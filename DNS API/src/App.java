package cr.ac.tec.ic7602.dnsapi.http;

import com.google.gson.Gson;
import cr.ac.tec.ic7602.dnsapi.dns.UdpDnsClient;
import io.javalin.http.Context;
import io.javalin.http.Handler;

import java.util.Base64;
import java.util.Map;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ExecutorService;

/**
 * POST /api/dns_resolver
 *
 * Body:     {"data": "<paquete DNS en BASE64>"}
 * Response: {"data": "<respuesta DNS en BASE64>"}
 *
 * El trabajo de red (UDP) se delega al executor de hilos virtuales para no
 * bloquear el hilo HTTP mientras se espera al servidor DNS remoto, lo que
 * permite atender múltiples solicitudes de forma simultánea.
 *
 * Nota: la forma exacta de manejar respuestas asíncronas (`ctx.future(...)`)
 * puede variar levemente según la versión de Javalin fijada en el pom.xml;
 * revisar la documentación de la versión exacta que use el equipo si este
 * método no compila tal cual.
 */
public class DnsResolverHandler implements Handler {

    private static final Gson GSON = new Gson();

    private final UdpDnsClient udpDnsClient;
    private final ExecutorService executor;

    public DnsResolverHandler(UdpDnsClient udpDnsClient, ExecutorService executor) {
        this.udpDnsClient = udpDnsClient;
        this.executor = executor;
    }

    @Override
    public void handle(Context ctx) {
        DnsResolverRequest body = GSON.fromJson(ctx.body(), DnsResolverRequest.class);

        if (body == null || body.data == null || body.data.isBlank()) {
            ctx.status(400).json(Map.of("error", "El campo 'data' (BASE64) es requerido"));
            return;
        }

        ctx.future(() -> CompletableFuture.runAsync(() -> resolveAndRespond(ctx, body.data), executor));
    }

    private void resolveAndRespond(Context ctx, String base64Data) {
        try {
            byte[] queryBytes = Base64.getDecoder().decode(base64Data);
            byte[] responseBytes = udpDnsClient.resolve(queryBytes);
            String encoded = Base64.getEncoder().encodeToString(responseBytes);

            ctx.json(Map.of("data", encoded));

        } catch (IllegalArgumentException e) {
            ctx.status(400).json(Map.of("error", "BASE64 invalido: " + e.getMessage()));
        } catch (UdpDnsClient.DnsResolutionException e) {
            ctx.status(504).json(Map.of("error", e.getMessage()));
        }
    }

    private static class DnsResolverRequest {
        String data;
    }
}
