package cr.ac.tec.ic7602.dnsapi.http;

import com.google.gson.JsonObject;
import cr.ac.tec.ic7602.dnsapi.firebase.FirebaseAuth;
import cr.ac.tec.ic7602.dnsapi.firebase.FirebaseDnsExistsChecker;
import io.javalin.http.Context;
import io.javalin.http.Handler;

import java.util.Map;

/**
 * GET /api/records/{hostname}
 *
 * Devuelve el contenido completo de los registros habilitados para el hostname
 * (tipo, targets, pesos, países...), que el DNS Interceptor necesita para aplicar
 * las estrategias single / multi / weight / round-trip / geo.
 *
 * Response 200: {"hostname": "...", "records": {"record-001": {...}}}
 * Response 404: si no hay registros habilitados.
 * Response 502: si Firebase no responde o rechaza la lectura.
 *
 * Pendiente: agregar el estado healthy/unhealthy leyendo /healthResults.
 */
public class RecordsHandler implements Handler {

    private final FirebaseDnsExistsChecker checker;

    public RecordsHandler(FirebaseDnsExistsChecker checker) {
        this.checker = checker;
    }

    @Override
    public void handle(Context ctx) {
        String hostname = ctx.pathParam("hostname");
        try {
            JsonObject records = checker.findEnabledByHostname(hostname);
            if (records.isEmpty()) {
                Json.send(ctx, 404, Map.of("hostname", hostname, "error", "Sin registros"));
                return;
            }
            JsonObject response = new JsonObject();
            response.addProperty("hostname", hostname);
            response.add("records", records);
            ctx.status(200).contentType("application/json").result(response.toString());
        } catch (FirebaseAuth.FirebaseException e) {
            Json.send(ctx, 502, Map.of("error", "Error consultando Firebase: " + e.getMessage()));
        }
    }
}
