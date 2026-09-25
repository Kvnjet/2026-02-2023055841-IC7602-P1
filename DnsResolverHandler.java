package cr.ac.tec.ic7602.dnsapi.http;

import com.google.gson.JsonSyntaxException;
import cr.ac.tec.ic7602.dnsapi.dns.UdpDnsClient;
import io.javalin.http.Context;
import io.javalin.http.Handler;

import java.util.Base64;
import java.util.Map;

/**
 * POST /api/dns_resolver
 *
 * Body:     {"data": "<paquete DNS en BASE64>"}
 * Response: {"data": "<respuesta DNS en BASE64>"}
 *
 * Jetty atiende cada petición en un hilo de su pool, así que varias
 * resoluciones pueden estar esperando al DNS remoto al mismo tiempo.
 */
public class DnsResolverHandler implements Handler {

    private final UdpDnsClient udpDnsClient;

    public DnsResolverHandler(UdpDnsClient udpDnsClient) {
        this.udpDnsClient = udpDnsClient;
    }

    @Override
    public void handle(Context ctx) {
        DnsResolverRequest body;
        try {
            body = Json.GSON.fromJson(ctx.body(), DnsResolverRequest.class);
        } catch (JsonSyntaxException e) {
            Json.send(ctx, 400, Map.of("error", "JSON invalido"));
            return;
        }
        if (body == null || body.data == null || body.data.isBlank()) {
            Json.send(ctx, 400, Map.of("error", "El campo 'data' (BASE64) es requerido"));
            return;
        }

        try {
            byte[] queryBytes = Base64.getDecoder().decode(body.data);
            byte[] responseBytes = udpDnsClient.resolve(queryBytes);
            Json.send(ctx, 200, Map.of("data", Base64.getEncoder().encodeToString(responseBytes)));
        } catch (IllegalArgumentException e) {
            Json.send(ctx, 400, Map.of("error", "BASE64 invalido: " + e.getMessage()));
        } catch (UdpDnsClient.DnsResolutionException e) {
            Json.send(ctx, 504, Map.of("error", e.getMessage()));
        }
    }

    private static class DnsResolverRequest {
        String data;
    }
}
