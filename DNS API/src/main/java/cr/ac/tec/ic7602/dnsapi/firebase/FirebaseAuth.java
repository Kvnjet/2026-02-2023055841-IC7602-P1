package cr.ac.tec.ic7602.dnsapi.firebase;

import com.google.gson.JsonObject;
import com.google.gson.JsonParser;

import java.net.URI;
import java.net.URLEncoder;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.nio.charset.StandardCharsets;
import java.time.Duration;
import java.time.Instant;

/**
 * Obtiene y cachea un ID token de Firebase Authentication (email/password)
 * usando la API REST. Las reglas de la Realtime Database exigen auth != null
 * para leer /dnsRecords, así que toda lectura necesita este token.
 *
 * Producción: FIREBASE_AUTH_URL vacío  -> https://identitytoolkit.googleapis.com
 * Emulador:   FIREBASE_AUTH_URL=http://host.docker.internal:9099
 *             -> http://host.docker.internal:9099/identitytoolkit.googleapis.com
 */
public class FirebaseAuth {

    private final HttpClient http;
    private final String signInUrl;
    private final String email;
    private final String password;

    private String idToken;
    private Instant expiresAt = Instant.EPOCH;

    public FirebaseAuth(HttpClient http, String authUrl, String apiKey, String email, String password) {
        this.http = http;
        String base = (authUrl == null || authUrl.isBlank())
                ? "https://identitytoolkit.googleapis.com"
                : authUrl.replaceAll("/+$", "") + "/identitytoolkit.googleapis.com";
        this.signInUrl = base + "/v1/accounts:signInWithPassword?key="
                + URLEncoder.encode(apiKey, StandardCharsets.UTF_8);
        this.email = email;
        this.password = password;
    }

    /** Devuelve un token válido; vuelve a iniciar sesión si falta menos de 1 min para que expire. */
    public synchronized String getIdToken() throws FirebaseException {
        if (idToken != null && Instant.now().isBefore(expiresAt.minusSeconds(60))) {
            return idToken;
        }
        JsonObject body = new JsonObject();
        body.addProperty("email", email);
        body.addProperty("password", password);
        body.addProperty("returnSecureToken", true);

        HttpRequest request = HttpRequest.newBuilder(URI.create(signInUrl))
                .timeout(Duration.ofSeconds(5))
                .header("Content-Type", "application/json")
                .POST(HttpRequest.BodyPublishers.ofString(body.toString()))
                .build();
        try {
            HttpResponse<String> response = http.send(request, HttpResponse.BodyHandlers.ofString());
            if (response.statusCode() != 200) {
                throw new FirebaseException("Firebase Auth HTTP " + response.statusCode() + ": " + response.body());
            }
            JsonObject json = JsonParser.parseString(response.body()).getAsJsonObject();
            idToken = json.get("idToken").getAsString();
            long expiresIn = Long.parseLong(json.get("expiresIn").getAsString()); // segundos, normalmente 3600
            expiresAt = Instant.now().plusSeconds(expiresIn);
            return idToken;
        } catch (FirebaseException e) {
            throw e;
        } catch (Exception e) {
            throw new FirebaseException("No se pudo autenticar contra Firebase", e);
        }
    }

    public static class FirebaseException extends Exception {
        private static final long serialVersionUID = 1L;
        public FirebaseException(String message) { super(message); }
        public FirebaseException(String message, Throwable cause) { super(message, cause); }
    }
}
