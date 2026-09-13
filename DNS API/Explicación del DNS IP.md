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
cp .env.example .env          # aquí hay que completar DB_URL / DB_API_KEY
export $(grep -v '^#' .env | xargs)
mvn clean package
java -jar target/dns-api.jar
```

## Ejecutar con Docker

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

## Estructura

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

## Pendiente para el equipo

- [ ] Definir el esquema real de la tabla en Supabase (`dns_records` es un nombre provisional) y ajustar `SupabaseDnsExistsChecker` en consecuencia.
- [ ] El enunciado solo exige `/api/exists` y `/api/dns_resolver` como mínimo, pero probablemente necesiten un endpoint adicional para que el Interceptor obtenga el **contenido** del registro (no solo su existencia) — p. ej. `GET /api/records/{domain}` — y así aplicar la lógica de `single`/`multi`/`weight`/`round-trip`/`geo`.
- [ ] Si usan Firebase en vez de Supabase, solo hace falta escribir una nueva clase que implemente `DnsExistsChecker` — el resto de la aplicación no debería cambiar.
- [ ] Pruebas unitarias (JUnit) para `UdpDnsClient` (con un socket UDP mock) y para los handlers HTTP.
- [ ] Verificar `ctx.future(...)` contra la versión exacta de Javalin que fijen — la API async ha cambiado entre versiones 5.x y 6.x.
- [ ] Decidir si vale la pena parsear el paquete de respuesta (RCODE, secciones) o si basta con reenviarlo tal cual, como está implementado ahora.

## Documentación de IA generativa

Este módulo fue generado con asistencia de IA como punto de partida para el equipo. Recuerden documentar en la sección correspondiente del proyecto los prompts utilizados y los ajustes/correcciones que hagan sobre este código, según lo exige el enunciado para las partes donde se permite el uso de IA.
