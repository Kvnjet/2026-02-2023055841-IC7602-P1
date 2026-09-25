# DNS API (Java)

Componente REST intermedio entre el **DNS Interceptor** (Rust), la base de datos **Firebase Realtime Database** y el servidor DNS remoto. Implementado con **Javalin** (sobre Jetty) y **Gson**, en Java 21.

Jetty atiende cada petición HTTP en un hilo de su pool, por lo que el API soporta múltiples peticiones concurrentes.

## Endpoints

| Método | Ruta | Descripción |
|---|---|---|
| `GET` | `/health` | Healthcheck (usado por Docker/Kubernetes) → `{"status":"UP"}` |
| `POST` | `/api/exists` | `{"domain": "..."}` → `{"domain": "...", "exists": true\|false}` |
| `GET` | `/api/records/{hostname}` | Contenido completo de los registros habilitados del hostname (404 si no hay) |
| `POST` | `/api/dns_resolver` | `{"data": "<BASE64>"}` → `{"data": "<BASE64>"}` (reenvía el paquete crudo al DNS remoto) |

Códigos de error: `400` petición inválida, `502` Firebase no respondió o rechazó la lectura, `504` timeout del DNS remoto.

## Conexión con Firebase

El DNS API usa la **API REST** de Firebase (igual que el Health Checker), sin SDK adicional:

1. **Autenticación.** Las reglas (`firebase/database.rules.json`) exigen `auth != null` para leer `/dnsRecords`. El API inicia sesión con email/contraseña en Firebase Auth (`accounts:signInWithPassword`) y obtiene un `idToken` válido por 1 hora. `FirebaseAuth` guarda el token y lo renueva cuando falta menos de un minuto para que expire.
2. **Consulta.** `FirebaseDnsExistsChecker` consulta:
   ```
   GET {DB_URL}/dnsRecords.json?orderBy="hostname"&equalTo="<host>"&auth=<token>[&ns=<namespace>]
   ```
   Usa el índice `.indexOn: ["hostname"]` de las reglas. Antes de consultar normaliza el dominio (minúsculas y sin el punto final que trae el paquete DNS) y descarta los registros con `enabled: false`.

### Variables de entorno

Toda la configuración viene de variables de entorno (ver `env.example`); no hay valores quemados en el código.

| Variable | Emulador (local) | Producción |
|---|---|---|
| `DNS_API_PORT` | `8080` | `8080` |
| `REMOTE_DNS_SERVER` / `REMOTE_DNS_PORT` | `8.8.8.8` / `53` | `8.8.8.8` / `53` |
| `UDP_TIMEOUT_MS` | `2000` | `2000` |
| `DB_PROVIDER` | `firebase` | `firebase` |
| `DB_URL` | `http://host.docker.internal:9000` | `https://p1-redes-a87a7-default-rtdb.firebaseio.com` |
| `DB_API_KEY` | `fake-api-key` | Web API Key del proyecto |
| `FIREBASE_AUTH_URL` | `http://host.docker.internal:9099` | (vacío) |
| `FIREBASE_DATABASE_NAMESPACE` | `p1-redes-a87a7-default-rtdb` | (vacío) |
| `FIREBASE_EMAIL` / `FIREBASE_PASSWORD` | usuario exclusivo del DNS API | usuario exclusivo del DNS API |

## Ejecutar

### Con Docker Compose (recomendado)

```bash
cd "DNS API"
cp env.example .env           # completar FIREBASE_EMAIL y FIREBASE_PASSWORD
docker compose up --build
```

El `compose.yaml` ya incluye `host.docker.internal:host-gateway` (necesario en Linux) y un healthcheck contra `/health`.

### Localmente (sin Docker)

Con el API fuera de Docker, en `.env` se usa `127.0.0.1` en lugar de `host.docker.internal`.

```bash
cd "DNS API"
export $(grep -v '^#' .env | xargs)
mvn clean package
java -jar target/dns-api.jar
```

## Pruebas

### 1. Pruebas unitarias (no necesitan Firebase)

`FirebaseDnsExistsCheckerTest` levanta un servidor HTTP falso (incluido en el JDK) que imita a Firebase y verifica cinco casos: registro existente, inexistente (`null`), deshabilitado, normalización del dominio junto con el armado de la consulta, y error 401.

```bash
cd "DNS API"
mvn test
# o, sin instalar Maven:
docker run --rm -v "$PWD":/app -w /app maven:3.9-eclipse-temurin-21 mvn test
```

### 2. Pruebas de integración (emulador + API corriendo)

1. Iniciar los emuladores: `cd firebase && firebase emulators:start`
2. Iniciar el API: `cd "DNS API" && docker compose up --build`
3. En otra terminal:

```bash
cd "DNS API"
FIREBASE_EMAIL=<el del .env> FIREBASE_PASSWORD=<el del .env> ./tests/pruebas_integracion.sh
```

El script carga registros de prueba en el emulador, crea el usuario del API si no existe y ejecuta estas verificaciones:

| # | Prueba | Esperado |
|---|---|---|
| 1 | Login y consulta directa a Firebase (sin Java) | token y registro `local-tcp.test` |
| 2 | `GET /health` | `200`, `"UP"` |
| 3 | `/api/exists` con `local-tcp.test` | `exists: true` |
| 4 | `/api/exists` con `LOCAL-TCP.test.` | `exists: true` |
| 5 | `/api/exists` con `no-existe.test` | `exists: false` |
| 6 | `/api/exists` con registro `enabled: false` | `exists: false` |
| 7 | `/api/exists` sin `domain` o con JSON inválido | `400` |
| 8 | `/api/records/local-tcp.test` | `200` con `"type":"single"` |
| 9 | `/api/records/no-existe.test` | `404` |
| 10 | `/api/dns_resolver` con consulta A de `google.com` | respuesta con el mismo ID (`0x1234`) y `RCODE=0` |
| 11 | `/api/dns_resolver` con BASE64 inválido o sin `data` | `400` |
| 12 | 50 peticiones simultáneas a `/api/dns_resolver` | 50 respuestas `200` |

Si la prueba 1 falla, el problema está en Firebase (emulador, usuario o reglas) y no en el código Java.

### Prueba manual rápida

```bash
curl http://localhost:8080/health

curl -X POST http://localhost:8080/api/exists \
  -H "Content-Type: application/json" \
  -d '{"domain": "local-tcp.test"}'

curl http://localhost:8080/api/records/local-tcp.test

curl -X POST http://localhost:8080/api/dns_resolver \
  -H "Content-Type: application/json" \
  -d '{"data": "EjQBAAABAAAAAAAABmdvb2dsZQNjb20AAAEAAQ=="}'
```

## Estructura

```
DNS API/
├── pom.xml
├── Dockerfile
├── compose.yaml
├── env.example
├── Funcionamiento DNS API.md
├── tests/
│   └── pruebas_integracion.sh              # pruebas de integración con curl
└── src/
    ├── main/java/cr/ac/tec/ic7602/dnsapi/
    │   ├── App.java                         # arranque del servidor y rutas
    │   ├── config/Config.java               # lectura de variables de entorno
    │   ├── dns/DnsExistsChecker.java        # interfaz de verificación de dominio
    │   ├── dns/UdpDnsClient.java            # cliente UDP hacia el DNS remoto
    │   ├── firebase/FirebaseAuth.java       # login y cache del token de Firebase
    │   ├── firebase/FirebaseDnsExistsChecker.java  # consultas a Realtime Database
    │   └── http/
    │       ├── Json.java                    # respuestas JSON con Gson
    │       ├── HealthHandler.java           # GET /health
    │       ├── ExistsHandler.java           # POST /api/exists
    │       ├── RecordsHandler.java          # GET /api/records/{hostname}
    │       └── DnsResolverHandler.java      # POST /api/dns_resolver
    └── test/java/cr/ac/tec/ic7602/dnsapi/
        └── firebase/FirebaseDnsExistsCheckerTest.java
```

## Pendientes

- [ ] Agregar a `/api/records/{hostname}` el estado healthy/unhealthy de cada target, leyendo `/healthResults/{recordId}/{targetId}` y aplicando mayoría simple entre Health Checkers.
- [ ] Que el DNS Interceptor consuma `/api/records/{hostname}` y `/api/dns_resolver`.
- [ ] Decidir si vale la pena parsear el paquete de respuesta (RCODE, secciones) o si basta con reenviarlo tal cual.
