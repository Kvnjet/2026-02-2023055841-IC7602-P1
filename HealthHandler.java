package cr.ac.tec.ic7602.dnsapi.http;

import io.javalin.http.Context;
import io.javalin.http.Handler;

import java.util.Map;

/** GET /health: usado por el healthcheck de Docker Compose / Kubernetes. */
public class HealthHandler implements Handler {
    @Override
    public void handle(Context ctx) {
        Json.send(ctx, 200, Map.of("status", "UP"));
    }
}
