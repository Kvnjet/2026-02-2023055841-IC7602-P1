package cr.ac.tec.ic7602.dnsapi.http;

import com.google.gson.JsonSyntaxException;
import cr.ac.tec.ic7602.dnsapi.dns.DnsExistsChecker;
import io.javalin.http.Context;
import io.javalin.http.Handler;

import java.util.Map;

/**
 * POST /api/exists
 *
 * Body:     {"domain": "ejemplo.com"}
 * Response: {"domain": "ejemplo.com", "exists": true|false}
 * Errores:  400 si falta el dominio, 502 si Firebase no responde o rechaza la lectura.
 */
public class ExistsHandler implements Handler {

    private final DnsExistsChecker checker;

    public ExistsHandler(DnsExistsChecker checker) {
        this.checker = checker;
    }

    @Override
    public void handle(Context ctx) {
        ExistsRequest body;
        try {
            body = Json.GSON.fromJson(ctx.body(), ExistsRequest.class);
        } catch (JsonSyntaxException e) {
            Json.send(ctx, 400, Map.of("error", "JSON invalido"));
            return;
        }
        if (body == null || body.domain == null || body.domain.isBlank()) {
            Json.send(ctx, 400, Map.of("error", "El campo 'domain' es requerido"));
            return;
        }

        try {
            boolean exists = checker.exists(body.domain);
            Json.send(ctx, 200, Map.of("domain", body.domain, "exists", exists));
        } catch (IllegalStateException e) {
            Json.send(ctx, 502, Map.of("error", "Error consultando Firebase: " + e.getMessage()));
        }
    }

    private static class ExistsRequest {
        String domain;
    }
}
