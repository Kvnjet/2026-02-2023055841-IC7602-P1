#ifndef HEALTH_CHECKER_CONFIG_H
#define HEALTH_CHECKER_CONFIG_H

#include <stdbool.h>

#define CONFIG_URL_CAPACITY 2048
#define CONFIG_VALUE_CAPACITY 512
#define CONFIG_EMAIL_CAPACITY 321
#define CONFIG_ID_CAPACITY 256
#define CONFIG_NAMESPACE_CAPACITY 128
#define CONFIG_CITY_CAPACITY 128
#define CONFIG_COUNTRY_CAPACITY 3

typedef struct {
    char firebase_database_url[CONFIG_URL_CAPACITY];
    char firebase_database_namespace[CONFIG_NAMESPACE_CAPACITY];
    char firebase_auth_url[CONFIG_URL_CAPACITY];
    char firebase_api_key[CONFIG_VALUE_CAPACITY];
    char firebase_checker_email[CONFIG_EMAIL_CAPACITY];
    char firebase_checker_password[CONFIG_VALUE_CAPACITY];
    char checker_id[CONFIG_ID_CAPACITY];
    char checker_city[CONFIG_CITY_CAPACITY];
    char checker_country[CONFIG_COUNTRY_CAPACITY];
    double checker_latitude;
    double checker_longitude;
} HealthCheckerConfig;

bool config_load(HealthCheckerConfig *config, const char *env_path);
void config_clear(HealthCheckerConfig *config);

#endif
