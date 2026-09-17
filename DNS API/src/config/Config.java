package cr.ac.tec.ic7602.dnsapi.config;

/**
 * Configuración de la aplicación, leída exclusivamente desde variables.
 *
 * Ver .env.example para las variables esperadas.
 */
public record Config(
        int port,
        String remoteDnsServer,
        int remoteDnsPort,
        int udpTimeoutMs,
        String dbProvider,
        String dbUrl,
        String dbApiKey
) {

    public static Config fromEnv() {
        return new Config(
                intEnv("DNS_API_PORT", 8080),
                strEnv("REMOTE_DNS_SERVER", "8.8.8.8"),
                intEnv("REMOTE_DNS_PORT", 53),
                intEnv("UDP_TIMEOUT_MS", 2000),
                strEnv("DB_PROVIDER", "supabase"),
                strEnv("DB_URL", ""),
                strEnv("DB_API_KEY", "")
        );
    }

    private static String strEnv(String key, String defaultValue) {
        String value = System.getenv(key);
        return (value == null || value.isBlank()) ? defaultValue : value;
    }

    private static int intEnv(String key, int defaultValue) {
        String value = System.getenv(key);
        if (value == null || value.isBlank()) {
            return defaultValue;
        }
        try {
            return Integer.parseInt(value.trim());
        } catch (NumberFormatException e) {
            System.err.printf("Valor invalido para %s: '%s', usando default %d%n", key, value, defaultValue);
            return defaultValue;
        }
    }
}
