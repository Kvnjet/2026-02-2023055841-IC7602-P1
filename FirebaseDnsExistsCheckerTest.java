package cr.ac.tec.ic7602.dnsapi.firebase;

import com.sun.net.httpserver.HttpExchange;
import com.sun.net.httpserver.HttpServer;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;

import java.io.IOException;
import java.io.OutputStream;
import java.net.InetSocketAddress;
import java.net.URLDecoder;
import java.net.http.HttpClient;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;

import static org.junit.jupiter.api.Assertions.*;

/**
 * Pruebas unitarias de FirebaseDnsExistsChecker.
 *
 * No usan Firebase real: levantan un servidor HTTP falso (incluido en el JDK)
 * que imita los endpoints de Firebase Auth y de Realtime Database. Así se prueba
 * la lógica del DNS API (armado de la consulta, normalización, filtro de enabled,
 * manejo de errores) sin depender de red ni del emulador.
 *
 * Ubicación: src/test/java/cr/ac/tec/ic7602/dnsapi/firebase/
 * Ejecutar:  mvn test
 */
class FirebaseDnsExistsCheckerTest {

    private HttpServer server;
    private FirebaseDnsExistsChecker checker;

    private final AtomicReference<String> dbResponse = new AtomicReference<>("null");
    private final AtomicInteger dbStatus = new AtomicInteger(200);
    private final AtomicReference<String> lastQuery = new AtomicReference<>();

    @BeforeEach
    void setUp() throws IOException {
        server = HttpServer.create(new InetSocketAddress("127.0.0.1", 0), 0);

        // Imita Firebase Auth (formato de URL del emulador)
        server.createContext("/identitytoolkit.googleapis.com/v1/accounts:signInWithPassword",
                ex -> reply(ex, 200, "{\"idToken\":\"token-falso\",\"expiresIn\":\"3600\"}"));

        // Imita Realtime Database y guarda la consulta recibida para revisarla
        server.createContext("/dnsRecords.json", ex -> {
            lastQuery.set(URLDecoder.decode(ex.getRequestURI().getRawQuery(), StandardCharsets.UTF_8));
            reply(ex, dbStatus.get(), dbResponse.get());
        });

        server.start();
        String base = "http://127.0.0.1:" + server.getAddress().getPort();

        HttpClient http = HttpClient.newHttpClient();
        FirebaseAuth auth = new FirebaseAuth(http, base, "fake-api-key", "dns-api@test.com", "secreto");
        checker = new FirebaseDnsExistsChecker(http, auth, base, "p1-redes-a87a7-default-rtdb");
    }

    @AfterEach
    void tearDown() {
        server.stop(0);
    }

    @Test
    void existeCuandoHayRegistroHabilitado() {
        dbResponse.set("{\"record-001\":{\"hostname\":\"local-tcp.test\",\"type\":\"single\",\"enabled\":true}}");
        assertTrue(checker.exists("local-tcp.test"));
    }

    @Test
    void noExisteCuandoFirebaseRespondeNull() {
        dbResponse.set("null");
        assertFalse(checker.exists("no-existe.test"));
    }

    @Test
    void ignoraRegistrosDeshabilitados() {
        dbResponse.set("{\"record-002\":{\"hostname\":\"apagado.test\",\"type\":\"single\",\"enabled\":false}}");
        assertFalse(checker.exists("apagado.test"));
    }

    @Test
    void normalizaDominioYArmaBienLaConsulta() {
        dbResponse.set("{\"record-001\":{\"hostname\":\"local-tcp.test\",\"enabled\":true}}");

        assertTrue(checker.exists("LOCAL-TCP.Test."));   // mayúsculas + punto final, como viene del paquete DNS

        String q = lastQuery.get();
        assertTrue(q.contains("orderBy=\"hostname\""), q);
        assertTrue(q.contains("equalTo=\"local-tcp.test\""), q);
        assertTrue(q.contains("auth=token-falso"), q);
        assertTrue(q.contains("ns=p1-redes-a87a7-default-rtdb"), q);
    }

    @Test
    void lanzaErrorSiFirebaseRechazaLaLectura() {
        dbStatus.set(401);
        dbResponse.set("{\"error\":\"Permission denied\"}");
        assertThrows(IllegalStateException.class, () -> checker.exists("local-tcp.test"));
    }

    private static void reply(HttpExchange ex, int status, String body) throws IOException {
        ex.getRequestBody().readAllBytes();
        byte[] bytes = body.getBytes(StandardCharsets.UTF_8);
        ex.getResponseHeaders().add("Content-Type", "application/json");
        ex.sendResponseHeaders(status, bytes.length);
        try (OutputStream os = ex.getResponseBody()) {
            os.write(bytes);
        }
    }
}
