package cr.ac.tec.ic7602.dnsapi;

import cr.ac.tec.ic7602.dnsapi.config.Config;
import cr.ac.tec.ic7602.dnsapi.dns.UdpDnsClient;
import cr.ac.tec.ic7602.dnsapi.firebase.FirebaseAuth;
import cr.ac.tec.ic7602.dnsapi.firebase.FirebaseDnsExistsChecker;
import cr.ac.tec.ic7602.dnsapi.http.DnsResolverHandler;
import cr.ac.tec.ic7602.dnsapi.http.ExistsHandler;
import cr.ac.tec.ic7602.dnsapi.http.HealthHandler;
import cr.ac.tec.ic7602.dnsapi.http.RecordsHandler;
import io.javalin.Javalin;

import java.net.http.HttpClient;
import java.time.Duration;

/**
 * Punto de entrada del DNS API.
 *
 * Toda la configuración viene de variables de entorno (ver env.example).
 * Javalin corre sobre Jetty, que atiende cada petición en su propio hilo del
 * pool, por lo que el API soporta múltiples peticiones concurrentes.
 */
public class App {

    public static void main(String[] args) {
        Config config = Config.fromEnv();

        if (!"firebase".equalsIgnoreCase(config.dbProvider())) {
            System.err.println("DB_PROVIDER='" + config.dbProvider() + "' no soportado. Use DB_PROVIDER=firebase");
            System.exit(1);
        }
        if (config.dbUrl().isBlank() || config.firebaseEmail().isBlank() || config.firebasePassword().isBlank()) {
            System.err.println("Faltan variables: DB_URL, FIREBASE_EMAIL y FIREBASE_PASSWORD son obligatorias");
            System.exit(1);
        }

        HttpClient http = HttpClient.newBuilder()
                .connectTimeout(Duration.ofSeconds(5))
                .build();

        FirebaseAuth auth = new FirebaseAuth(http, config.firebaseAuthUrl(),
                config.dbApiKey(), config.firebaseEmail(), config.firebasePassword());
        FirebaseDnsExistsChecker checker = new FirebaseDnsExistsChecker(http, auth,
                config.dbUrl(), config.firebaseNamespace());
        UdpDnsClient udpDnsClient = new UdpDnsClient(
                config.remoteDnsServer(), config.remoteDnsPort(), config.udpTimeoutMs());

        Javalin app = Javalin.create();
        app.get("/health", new HealthHandler());
        app.post("/api/exists", new ExistsHandler(checker));
        app.get("/api/records/{hostname}", new RecordsHandler(checker));
        app.post("/api/dns_resolver", new DnsResolverHandler(udpDnsClient));
        app.start(config.port());

        System.out.printf("DNS API escuchando en el puerto %d (DNS remoto %s:%d)%n",
                config.port(), config.remoteDnsServer(), config.remoteDnsPort());
    }
}
