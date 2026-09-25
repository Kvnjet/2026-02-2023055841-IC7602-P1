#include "config.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define TEST_ENV_PATH "config_test.env"

static int failures = 0;

static void expect_true(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        failures++;
    }
}

static int write_env(const char *contents)
{
    FILE *file = fopen(TEST_ENV_PATH, "w");

    if (file == NULL) {
        return 0;
    }
    if (fputs(contents, file) == EOF) {
        fclose(file);
        return 0;
    }
    return fclose(file) == 0;
}

static const char *valid_env(void)
{
    return
        "# Health Checker test configuration\n"
        "FIREBASE_DATABASE_URL=http://host.docker.internal:9000\n"
        "FIREBASE_DATABASE_NAMESPACE=p1-redes-a87a7-default-rtdb\n"
        "FIREBASE_AUTH_URL=http://host.docker.internal:9099\n"
        "FIREBASE_API_KEY=fake-api-key\n"
        "FIREBASE_CHECKER_EMAIL=checker-cartago@example.com\n"
        "FIREBASE_CHECKER_PASSWORD='password with spaces'\n"
        "CHECKER_ID=checker-cartago\n"
        "CHECKER_CITY=Cartago\n"
        "CHECKER_COUNTRY=cr\n"
        "CHECKER_LATITUDE=9.8644\n"
        "CHECKER_LONGITUDE=-83.9194\n";
}

static void test_valid_config(void)
{
    HealthCheckerConfig config;

    expect_true(write_env(valid_env()), "write valid fixture");
    expect_true(config_load(&config, TEST_ENV_PATH), "load valid config");
    expect_true(strcmp(config.firebase_database_url,
                       "http://host.docker.internal:9000") == 0,
                "database URL is loaded");
    expect_true(strcmp(config.firebase_database_namespace,
                       "p1-redes-a87a7-default-rtdb") == 0,
                "database namespace is loaded");
    expect_true(strcmp(config.firebase_checker_password,
                       "password with spaces") == 0,
                "quoted value is loaded");
    expect_true(strcmp(config.checker_country, "CR") == 0,
                "country code is normalized");
    expect_true(fabs(config.checker_latitude - 9.8644) < 0.000001,
                "latitude is parsed");
    expect_true(fabs(config.checker_longitude + 83.9194) < 0.000001,
                "longitude is parsed");
}

static void test_missing_required_value(void)
{
    HealthCheckerConfig config;
    const char *contents =
        "FIREBASE_DATABASE_URL=http://host.docker.internal:9000\n"
        "FIREBASE_DATABASE_NAMESPACE=p1-redes-a87a7-default-rtdb\n"
        "FIREBASE_AUTH_URL=http://host.docker.internal:9099\n"
        "FIREBASE_API_KEY=fake-api-key\n"
        "FIREBASE_CHECKER_EMAIL=checker-cartago@example.com\n"
        "FIREBASE_CHECKER_PASSWORD=secret\n"
        "CHECKER_ID=\n"
        "CHECKER_CITY=Cartago\n"
        "CHECKER_COUNTRY=CR\n"
        "CHECKER_LATITUDE=9.8644\n"
        "CHECKER_LONGITUDE=-83.9194\n";

    expect_true(write_env(contents), "write missing-value fixture");
    expect_true(!config_load(&config, TEST_ENV_PATH),
                "reject empty required value");
    expect_true(config.checker_city[0] == '\0',
                "clear output after failed load");
}

static void test_invalid_coordinate(void)
{
    HealthCheckerConfig config;
    const char *contents =
        "FIREBASE_DATABASE_URL=http://host.docker.internal:9000\n"
        "FIREBASE_DATABASE_NAMESPACE=p1-redes-a87a7-default-rtdb\n"
        "FIREBASE_AUTH_URL=http://host.docker.internal:9099\n"
        "FIREBASE_API_KEY=fake-api-key\n"
        "FIREBASE_CHECKER_EMAIL=checker-cartago@example.com\n"
        "FIREBASE_CHECKER_PASSWORD=secret\n"
        "CHECKER_ID=checker-cartago\n"
        "CHECKER_CITY=Cartago\n"
        "CHECKER_COUNTRY=CR\n"
        "CHECKER_LATITUDE=91\n"
        "CHECKER_LONGITUDE=-83.9194\n";

    expect_true(write_env(contents), "write invalid-coordinate fixture");
    expect_true(!config_load(&config, TEST_ENV_PATH),
                "reject out-of-range coordinate");
}

int main(void)
{
    test_valid_config();
    test_missing_required_value();
    test_invalid_coordinate();

    remove(TEST_ENV_PATH);
    if (failures != 0) {
        fprintf(stderr, "%d config test(s) failed.\n", failures);
        return 1;
    }

    puts("All config tests passed.");
    return 0;
}
