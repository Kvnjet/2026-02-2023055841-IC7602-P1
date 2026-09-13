# DNS API (Java)

Componente REST intermedio entre el **DNS Interceptor** (Rust) y el servidor DNS remoto. Implementado con **Javalin** + hilos virtuales (Java 21) para soportar múltiples peticiones concurrentes sin bloquear el servidor HTTP.

## Endpoints

| Método | Ruta | Descripción |
|---|---|---|
| `GET` | `/health` | Healthcheck (usado por Docker/Kubernetes) |
| `POST` | `/api/exists` | `{"domain": "..."}` → `{"domain": "...", "exists": true\|false}` |
| `POST` | `/api/dns_resolver` | `{"data": "<BASE64>"}` → `{"data": "<BASE64>"}` (reenvía el paquete crudo al DNS remoto) |

## Ejecutar localmente

```bash
cd dns-api
cp .env.example .env          # completar DB_URL / DB_API_KEY
export $(grep -v '^#' .env | xargs)
mvn clean package
java -jar target/dns-api.jar
```

## Ejecutar con Docker (Yo no lo descargo porque ya lo tengo)

```bash
docker build -t dns-api .
docker run --rm -p 8080:8080 --env-file .env dns-api
```

## Probar manualmente

```bash
curl http://localhost:8080/health

curl -X POST http://localhost:8080/api/exists \
  -H "Content-Type: application/json" \
  -d '{"domain": "ejemplo.midominio.com"}'
```

## Estructura (el diagrama de donde encontrar varas)

```
dns-api/
├── pom.xml
├── Dockerfile
├── .env.example
└── src/
    ├── App.java                        # arranque del servidor
    ├── config/Config.java              # lectura de variables de entorno
    ├── dns/UdpDnsClient.java           # cliente UDP hacia el DNS remoto
    ├── dns/DnsExistsChecker.java       # interfaz de verificación de dominio
    ├── dns/SupabaseDnsExistsChecker.java  # implementación (stub funcional)
    └── http/
        ├── DnsResolverHandler.java     # POST /api/dns_resolver
        ├── ExistsHandler.java          # POST /api/exists
        └── HealthHandler.java          # GET /health
```

## Pendientes que todavía no se su implementación

- [ ] Definir el esquema real de la tabla en Supabase (`dns_records` es un nombre inventado) y ajustar `SupabaseDnsExistsChecker` a lo que toque
- [ ] probablemente necesitemos un endpoint adicional para que el Interceptor obtenga el contenido del registro (no solo una confirmación de que ahí está) ej. `GET /api/records/{domain}`— y así aplicar la lógica de `single`/`multi`/`weight`/`round-trip`/`geo`
- [ ] Si usamos Firebase en vez de Supabase, solo hace falta escribir una nueva clase que implemente eso de `DnsExistsChecker`, el resto DEBERÍA funcionar
- [ ] Verificar `ctx.future(...)` contra la versión exacta de Javalin que se use
- [ ] Decidir si vale la pena parsear el paquete de respuesta (RCODE, secciones) o si basta con reenviarlo tal cual, como tengo por ahora porque no se

