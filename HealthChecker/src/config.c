#include "config.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ENV_LINE_CAPACITY 4096
#define RAW_VALUE_CAPACITY CONFIG_URL_CAPACITY

typedef enum {
    KEY_FIREBASE_DATABASE_URL,
    KEY_FIREBASE_DATABASE_NAMESPACE,
    KEY_FIREBASE_AUTH_URL,
    KEY_FIREBASE_API_KEY,
    KEY_FIREBASE_CHECKER_EMAIL,
    KEY_FIREBASE_CHECKER_PASSWORD,
    KEY_CHECKER_ID,
    KEY_CHECKER_CITY,
    KEY_CHECKER_COUNTRY,
    KEY_CHECKER_LATITUDE,
    KEY_CHECKER_LONGITUDE,
    KEY_COUNT
} ConfigKey;

static const char *const REQUIRED_KEYS[KEY_COUNT] = {
    "FIREBASE_DATABASE_URL",
    "FIREBASE_DATABASE_NAMESPACE",
    "FIREBASE_AUTH_URL",
    "FIREBASE_API_KEY",
    "FIREBASE_CHECKER_EMAIL",
    "FIREBASE_CHECKER_PASSWORD",
    "CHECKER_ID",
    "CHECKER_CITY",
    "CHECKER_COUNTRY",
    "CHECKER_LATITUDE",
    "CHECKER_LONGITUDE"
};

static char *trim_left(char *text)
{
    while (*text != '\0' && isspace((unsigned char)*text)) {
        text++;
    }
    return text;
}

static void trim_right(char *text)
{
    size_t length = strlen(text);

    while (length > 0 && isspace((unsigned char)text[length - 1])) {
        text[--length] = '\0';
    }
}

static int key_index(const char *key)
{
    int index;

    for (index = 0; index < KEY_COUNT; index++) {
        if (strcmp(key, REQUIRED_KEYS[index]) == 0) {
            return index;
        }
    }
    return -1;
}

static bool append_character(char *output, size_t output_capacity,
                             size_t *output_length, char character,
                             size_t line_number)
{
    if (*output_length + 1 >= output_capacity) {
        fprintf(stderr, "Configuration error on line %zu: value is too long.\n",
                line_number);
        return false;
    }

    output[(*output_length)++] = character;
    return true;
}

static bool parse_quoted_value(const char *input, char quote, char *output,
                               size_t output_capacity, size_t line_number)
{
    size_t input_index = 1;
    size_t output_length = 0;

    while (input[input_index] != '\0' && input[input_index] != quote) {
        char character = input[input_index++];

        if (quote == '"' && character == '\\') {
            char escaped = input[input_index++];

            if (escaped == '\0') {
                fprintf(stderr,
                        "Configuration error on line %zu: unfinished escape sequence.\n",
                        line_number);
                return false;
            }

            switch (escaped) {
            case 'n':
                character = '\n';
                break;
            case 'r':
                character = '\r';
                break;
            case 't':
                character = '\t';
                break;
            case '\\':
            case '"':
                character = escaped;
                break;
            default:
                fprintf(stderr,
                        "Configuration error on line %zu: unsupported escape sequence.\n",
                        line_number);
                return false;
            }
        }

        if (!append_character(output, output_capacity, &output_length,
                              character, line_number)) {
            return false;
        }
    }

    if (input[input_index] != quote) {
        fprintf(stderr, "Configuration error on line %zu: missing closing quote.\n",
                line_number);
        return false;
    }

    input_index++;
    while (isspace((unsigned char)input[input_index])) {
        input_index++;
    }
    if (input[input_index] != '\0' && input[input_index] != '#') {
        fprintf(stderr,
                "Configuration error on line %zu: unexpected text after quoted value.\n",
                line_number);
        return false;
    }

    output[output_length] = '\0';
    return true;
}

static bool parse_unquoted_value(char *input, char *output,
                                 size_t output_capacity, size_t line_number)
{
    size_t index;

    for (index = 0; input[index] != '\0'; index++) {
        if (input[index] == '#' &&
            (index == 0 || isspace((unsigned char)input[index - 1]))) {
            input[index] = '\0';
            break;
        }
    }

    trim_right(input);
    if (strlen(input) >= output_capacity) {
        fprintf(stderr, "Configuration error on line %zu: value is too long.\n",
                line_number);
        return false;
    }

    strcpy(output, input);
    return true;
}

static bool parse_value(char *input, char *output, size_t output_capacity,
                        size_t line_number)
{
    input = trim_left(input);
    trim_right(input);

    if (*input == '\'' || *input == '"') {
        return parse_quoted_value(input, *input, output, output_capacity,
                                  line_number);
    }

    return parse_unquoted_value(input, output, output_capacity, line_number);
}

static bool read_values(FILE *env_file,
                        char values[KEY_COUNT][RAW_VALUE_CAPACITY])
{
    char line[ENV_LINE_CAPACITY];
    bool seen[KEY_COUNT] = {false};
    size_t line_number = 0;

    while (fgets(line, sizeof(line), env_file) != NULL) {
        char *content;
        char *separator;
        int index;

        line_number++;
        if (strchr(line, '\n') == NULL && !feof(env_file)) {
            fprintf(stderr,
                    "Configuration error on line %zu: line exceeds %d characters.\n",
                    line_number, ENV_LINE_CAPACITY - 2);
            return false;
        }

        content = line;
        if (line_number == 1 &&
            (unsigned char)content[0] == 0xEF &&
            (unsigned char)content[1] == 0xBB &&
            (unsigned char)content[2] == 0xBF) {
            content += 3;
        }

        content = trim_left(content);
        trim_right(content);
        if (*content == '\0' || *content == '#') {
            continue;
        }

        if (strncmp(content, "export", 6) == 0 &&
            isspace((unsigned char)content[6])) {
            content = trim_left(content + 6);
        }

        separator = strchr(content, '=');
        if (separator == NULL) {
            fprintf(stderr,
                    "Configuration error on line %zu: expected KEY=VALUE.\n",
                    line_number);
            return false;
        }

        *separator = '\0';
        trim_right(content);
        if (*content == '\0') {
            fprintf(stderr,
                    "Configuration error on line %zu: variable name is empty.\n",
                    line_number);
            return false;
        }

        index = key_index(content);
        if (index < 0) {
            continue;
        }
        if (seen[index]) {
            fprintf(stderr, "Configuration error: %s is defined more than once.\n",
                    REQUIRED_KEYS[index]);
            return false;
        }

        if (!parse_value(separator + 1, values[index],
                         sizeof(values[index]), line_number)) {
            return false;
        }
        seen[index] = true;
    }

    if (ferror(env_file)) {
        fprintf(stderr, "Configuration error: failed while reading the .env file.\n");
        return false;
    }

    for (int index = 0; index < KEY_COUNT; index++) {
        if (!seen[index] || values[index][0] == '\0') {
            fprintf(stderr,
                    "Configuration error: required variable %s is missing or empty.\n",
                    REQUIRED_KEYS[index]);
            return false;
        }
    }

    return true;
}

static bool copy_value(char *destination, size_t capacity, const char *value,
                       const char *key)
{
    size_t length = strlen(value);

    if (length >= capacity) {
        fprintf(stderr, "Configuration error: %s exceeds %zu characters.\n",
                key, capacity - 1);
        return false;
    }

    memcpy(destination, value, length + 1);
    return true;
}

static bool is_http_url(const char *value)
{
    const char *host;

    if (strncmp(value, "http://", 7) == 0) {
        host = value + 7;
    } else if (strncmp(value, "https://", 8) == 0) {
        host = value + 8;
    } else {
        return false;
    }

    return *host != '\0' && *host != '/';
}

static bool parse_coordinate(const char *value, const char *key,
                             double minimum, double maximum, double *result)
{
    char *end;
    double parsed;

    errno = 0;
    parsed = strtod(value, &end);
    if (errno == ERANGE || end == value || *end != '\0' || !isfinite(parsed) ||
        parsed < minimum || parsed > maximum) {
        fprintf(stderr,
                "Configuration error: %s must be a number between %.0f and %.0f.\n",
                key, minimum, maximum);
        return false;
    }

    *result = parsed;
    return true;
}

static bool has_valid_email_shape(const char *email)
{
    const char *at = strchr(email, '@');

    return at != NULL && at != email && at[1] != '\0' &&
           strchr(at + 1, '@') == NULL;
}

static bool has_valid_firebase_key(const char *value)
{
    return strpbrk(value, ".#$[]/") == NULL;
}

static bool has_valid_database_namespace(const char *value)
{
    size_t index;
    size_t length = strlen(value);

    if (length == 0 || !isalnum((unsigned char)value[0]) ||
        !isalnum((unsigned char)value[length - 1])) {
        return false;
    }
    for (index = 0; index < length; index++) {
        if (!isalnum((unsigned char)value[index]) && value[index] != '-') {
            return false;
        }
    }
    return true;
}

static bool validate_and_copy(
    HealthCheckerConfig *config,
    char values[KEY_COUNT][RAW_VALUE_CAPACITY])
{
    const char *country = values[KEY_CHECKER_COUNTRY];

    if (!is_http_url(values[KEY_FIREBASE_DATABASE_URL])) {
        fprintf(stderr,
                "Configuration error: FIREBASE_DATABASE_URL must be an http:// or https:// URL.\n");
        return false;
    }
    if (!has_valid_database_namespace(
            values[KEY_FIREBASE_DATABASE_NAMESPACE])) {
        fprintf(stderr,
                "Configuration error: FIREBASE_DATABASE_NAMESPACE must contain only letters, numbers, and internal hyphens.\n");
        return false;
    }
    if (!is_http_url(values[KEY_FIREBASE_AUTH_URL])) {
        fprintf(stderr,
                "Configuration error: FIREBASE_AUTH_URL must be an http:// or https:// URL.\n");
        return false;
    }
    if (!has_valid_email_shape(values[KEY_FIREBASE_CHECKER_EMAIL])) {
        fprintf(stderr,
                "Configuration error: FIREBASE_CHECKER_EMAIL must contain one @ with text on both sides.\n");
        return false;
    }
    if (!has_valid_firebase_key(values[KEY_CHECKER_ID])) {
        fprintf(stderr,
                "Configuration error: CHECKER_ID contains a character forbidden in a Firebase key.\n");
        return false;
    }
    if (strlen(country) != 2 || !isalpha((unsigned char)country[0]) ||
        !isalpha((unsigned char)country[1])) {
        fprintf(stderr,
                "Configuration error: CHECKER_COUNTRY must be a two-letter country code.\n");
        return false;
    }

    if (!copy_value(config->firebase_database_url,
                    sizeof(config->firebase_database_url),
                    values[KEY_FIREBASE_DATABASE_URL],
                    REQUIRED_KEYS[KEY_FIREBASE_DATABASE_URL]) ||
        !copy_value(config->firebase_database_namespace,
                    sizeof(config->firebase_database_namespace),
                    values[KEY_FIREBASE_DATABASE_NAMESPACE],
                    REQUIRED_KEYS[KEY_FIREBASE_DATABASE_NAMESPACE]) ||
        !copy_value(config->firebase_auth_url,
                    sizeof(config->firebase_auth_url),
                    values[KEY_FIREBASE_AUTH_URL],
                    REQUIRED_KEYS[KEY_FIREBASE_AUTH_URL]) ||
        !copy_value(config->firebase_api_key,
                    sizeof(config->firebase_api_key),
                    values[KEY_FIREBASE_API_KEY],
                    REQUIRED_KEYS[KEY_FIREBASE_API_KEY]) ||
        !copy_value(config->firebase_checker_email,
                    sizeof(config->firebase_checker_email),
                    values[KEY_FIREBASE_CHECKER_EMAIL],
                    REQUIRED_KEYS[KEY_FIREBASE_CHECKER_EMAIL]) ||
        !copy_value(config->firebase_checker_password,
                    sizeof(config->firebase_checker_password),
                    values[KEY_FIREBASE_CHECKER_PASSWORD],
                    REQUIRED_KEYS[KEY_FIREBASE_CHECKER_PASSWORD]) ||
        !copy_value(config->checker_id, sizeof(config->checker_id),
                    values[KEY_CHECKER_ID], REQUIRED_KEYS[KEY_CHECKER_ID]) ||
        !copy_value(config->checker_city, sizeof(config->checker_city),
                    values[KEY_CHECKER_CITY], REQUIRED_KEYS[KEY_CHECKER_CITY])) {
        return false;
    }

    config->checker_country[0] = (char)toupper((unsigned char)country[0]);
    config->checker_country[1] = (char)toupper((unsigned char)country[1]);
    config->checker_country[2] = '\0';

    return parse_coordinate(values[KEY_CHECKER_LATITUDE],
                            REQUIRED_KEYS[KEY_CHECKER_LATITUDE], -90.0, 90.0,
                            &config->checker_latitude) &&
           parse_coordinate(values[KEY_CHECKER_LONGITUDE],
                            REQUIRED_KEYS[KEY_CHECKER_LONGITUDE], -180.0, 180.0,
                            &config->checker_longitude);
}

bool config_load(HealthCheckerConfig *config, const char *env_path)
{
    char values[KEY_COUNT][RAW_VALUE_CAPACITY] = {{0}};
    FILE *env_file;
    bool success;

    if (config == NULL) {
        fprintf(stderr, "Configuration error: config output cannot be NULL.\n");
        return false;
    }
    memset(config, 0, sizeof(*config));

    if (env_path == NULL || *env_path == '\0') {
        fprintf(stderr, "Configuration error: .env path cannot be empty.\n");
        return false;
    }

    env_file = fopen(env_path, "r");
    if (env_file == NULL) {
        fprintf(stderr, "Configuration error: cannot open %s: %s.\n", env_path,
                strerror(errno));
        return false;
    }

    success = read_values(env_file, values) && validate_and_copy(config, values);
    if (fclose(env_file) != 0) {
        fprintf(stderr, "Configuration error: could not close %s: %s.\n",
                env_path, strerror(errno));
        success = false;
    }

    if (!success) {
        memset(config, 0, sizeof(*config));
    }
    return success;
}

void config_clear(HealthCheckerConfig *config)
{
    volatile unsigned char *cursor;
    size_t remaining;

    if (config == NULL) {
        return;
    }

    cursor = (volatile unsigned char *)config;
    remaining = sizeof(*config);
    while (remaining > 0) {
        *cursor++ = 0;
        remaining--;
    }
}
