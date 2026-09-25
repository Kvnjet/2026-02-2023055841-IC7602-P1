package cr.ac.tec.ic7602.dnsapi.firebase;

import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import cr.ac.tec.ic7602.dnsapi.dns.DnsExistsChecker;

import java.net.URI;
import java.net.URLEncoder;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.nio.charset.StandardCharsets;
import java.time.Duration;
import java.util.Locale;
import java.util.Map;

/**
 * Implementación de DnsExistsChecker sobre la API REST de Firebase Realtime Database.
 *
 * Consulta: GET {DB_URL}/dnsRecords.json?orderBy="hostname"&equalTo="<host>"&auth=<token>[&ns=<namespace>]
 * Requiere ".indexOn": ["hostname"] en database.rules.json (ya está).
 */
public class FirebaseDnsExistsChecker implements DnsExistsChecker {

    private final HttpClient http;
    private final FirebaseAuth auth;
    private final String dbUrl;      // p.ej. https://p1-redes-a87a7-default-rtdb.firebaseio.com o http://host.docker.internal:9000
    private final String namespace;  // solo para el emulador, p.ej. p1-redes-a87a7-default-rtdb; vacío en producción

    public FirebaseDnsExistsChecker(HttpClient http, FirebaseAuth auth, String dbUrl, String namespace) {
        this.http = http;
        this.auth = auth;
        this.dbUrl = dbUrl.replaceAll("/+$", "");
        this.namespace = namespace == null ? "" : namespace;
    }

    @Override
    public boolean exists(String domain) {
        try {
            return !findEnabledByHostname(domain).isEmpty();
        } catch (FirebaseAuth.FirebaseException e) {
            throw new IllegalStateException(e.getMessage(), e);
        }
    }

    /**
     * Devuelve los registros habilitados cuyo hostname coincide, como {recordId: {...}}.
     * Útil también para un futuro GET /api/records/{hostname} que necesite el contenido.
     */
    public JsonObject findEnabledByHostname(String domain) throws FirebaseAuth.FirebaseException {
        String hostname = normalize(domain);
        String query = "orderBy=" + enc("\"hostname\"")
                + "&equalTo=" + enc("\"" + hostname + "\"")
                + "&auth=" + enc(auth.getIdToken());
        if (!namespace.isBlank()) {
            query += "&ns=" + enc(namespace);
        }

        HttpRequest request = HttpRequest.newBuilder(URI.create(dbUrl + "/dnsRecords.json?" + query))
                .timeout(Duration.ofSeconds(5))
                .GET()
                .build();
        try {
            HttpResponse<String> response = http.send(request, HttpResponse.BodyHandlers.ofString());
            if (response.statusCode() != 200) {
                throw new FirebaseAuth.FirebaseException(
                        "Realtime Database HTTP " + response.statusCode() + ": " + response.body());
            }
            JsonElement root = JsonParser.parseString(response.body());
            JsonObject result = new JsonObject();
            if (root.isJsonObject()) {                       // "null" si no hay coincidencias
                for (Map.Entry<String, JsonElement> e : root.getAsJsonObject().entrySet()) {
                    JsonObject record = e.getValue().getAsJsonObject();
                    boolean enabled = !record.has("enabled") || record.get("enabled").getAsBoolean();
                    if (enabled) {
                        result.add(e.getKey(), record);
                    }
                }
            }
            return result;
        } catch (FirebaseAuth.FirebaseException e) {
            throw e;
        } catch (Exception e) {
            throw new FirebaseAuth.FirebaseException("Error consultando Realtime Database", e);
        }
    }

    /** "WWW.Ejemplo.com." -> "www.ejemplo.com" (los nombres DNS no distinguen mayúsculas). */
    private static String normalize(String domain) {
        String d = domain.trim().toLowerCase(Locale.ROOT);
        return d.endsWith(".") ? d.substring(0, d.length() - 1) : d;
    }

    private static String enc(String s) {
        return URLEncoder.encode(s, StandardCharsets.UTF_8);
    }
}
