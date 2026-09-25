# Investigación: Implementación de la DNS API y resolución de IPs

**Proyecto 1  Redes**

Este documento resume mi investigación técnica sobre cómo implementar el componente **DNS API** (Java) del sistema, incluyendo el manejo de paquetes DNS, el cliente UDP hacia el servidor remoto, y la lógica de resolución de IP para cada tipo de registro (`single`, `multi`, `weight`, `round-trip`, `geo`).

---

## 1. Formato del paquete DNS (RFC1035 / RFC2929)

Todo paquete DNS tiene:

- Una **cabecera fija de 12 bytes**: ID de transacción, flags (QR, OPCODE, AA, TC, RD, RA, RCODE) y cuatro contadores de secciones (QDCOUNT, ANCOUNT, NSCOUNT, ARCOUNT).
- Cuatro **secciones variables**: Question, Answer, Authority y Additional.
- Los nombres de dominio se codifican como una secuencia de "labels" con longitud precedida, y pueden usar **compresión por punteros** (dos bits en 1 más un offset de 14 bits) para referenciar un nombre ya presente en el paquete.

Este formato es el mismo que debe decodificar el DNS Interceptor antes de reenviar el paquete a la DNS API en BASE64, y el que la DNS API debe volver a interpretar para construir su propia consulta hacia el DNS remoto.

### Dos caminos de implementación

**a) Manual (recomendado para entender lo que van a defender en la evaluación presencial):**
Leer los 12 bytes de cabecera con desplazamiento de bits, parsear el nombre siguiendo el formato de labels, y repetir para cada sección. Es más código, pero es exactamente el conocimiento que la evaluación oral va a examinar.

**b) Con librería (para acelerar el desarrollo, documentando su uso como pide el enunciado):**
- **Java** — `dnsjava`: soporta prácticamente todos los tipos de registro (incluido DNSSEC), es thread-safe, e incluye tanto un resolver UDP con fallback a TCP como utilidades para construir/parsear mensajes DNS crudos. Es una librería completa, por lo que para este proyecto probablemente solo necesiten las clases `Message` y `Record`, no todo el paquete.
- **Rust** (para el DNS Interceptor) — `trust-dns-proto` es la opción más usada junto con `tokio::net::UdpSocket` para el listener asíncrono en UDP/53; alternativas más ligeras como `dns-parser` o `rusdig` solo hacen parsing/construcción del mensaje, sin manejar la red.

> Si usan una librería, el enunciado exige documentar su uso — no basta con solo importarla y no explicarla en la sustentación.

---

## 2. Cliente UDP dentro de la DNS API (Java)

El endpoint `/api/dns_resolver` debe:

1. Recibir el paquete DNS en BASE64 vía POST.
2. Decodificarlo a bytes.
3. Enviarlo por UDP al servidor DNS remoto configurado (ej. `8.8.8.8:53`) usando `DatagramSocket`/`DatagramPacket` de `java.net`.
4. Esperar la respuesta con un **timeout** (`socket.setSoTimeout(ms)`) para no dejar conexiones colgadas si el servidor remoto no responde.
5. Re-codificar la respuesta en BASE64 y devolverla al Interceptor.

**Concurrencia:** el enunciado exige soportar múltiples peticiones simultáneas. El patrón recomendado es no bloquear el hilo que atiende la petición HTTP mientras se espera la respuesta UDP — usando un `ExecutorService` (thread pool) o hilos virtuales de Java 21+ para delegar cada resolución UDP, liberando el hilo HTTP principal.

**Framework REST:** para un servicio con pocos endpoints, un framework ligero como **Javalin** evita la sobrecarga de un contenedor de inyección de dependencias y arranque lento; **Spring Boot** es una alternativa más "conocida" académicamente, con más funcionalidad integrada (validación, manejo de errores) a costa de mayor complejidad y tiempo de arranque. Ambas opciones son razonables para el alcance del proyecto.

---

## 3. Lógica de resolución de IP por tipo de registro

Esta lógica normalmente vive en el DNS Interceptor (que consulta a la DNS API para saber si el dominio existe y qué tipo de registro tiene), pero puede repartirse entre ambos componentes según cómo diseñe el equipo la separación de responsabilidades:

| Tipo | Estrategia de implementación |
|---|---|
| `single` | Retornar directamente la única IP almacenada. |
| `multi` | Mantener un índice/contador (protegido con lock o variable atómica si hay concurrencia) que rote entre las IPs disponibles en cada consulta (round-robin). |
| `weight` | Sumar los pesos de todas las IPs, generar un número aleatorio dentro de ese rango, y recorrer la lista acumulando pesos hasta superar el valor aleatorio — la IP en la que se "cae" es la elegida. |
| `geo` | Tomar la IP de origen del paquete UDP recibido, buscarla en la tabla IP-to-Country (cargada en Supabase/Firebase), y filtrar el registro correspondiente al país. Si no hay IP para ese país, elegir una al azar entre todas las disponibles. |
| `round-trip` | Comparar la última latencia reportada por cada Health Checker (almacenada en la base de datos) para el dominio en cuestión, y devolver la IP asociada al Health Checker con menor tiempo de round-trip. |

En todos los casos, si el registro no existe o la IP seleccionada está marcada `unhealthy` por el Health Checker, la solicitud debe tratarse como un query no estándar y reenviarse tal cual a la DNS API para resolución externa (según lo indicado en el enunciado).

---

## 4. Automatización de la DNS API con Docker

**Dockerfile multi-stage:**
```dockerfile
FROM maven:3.9-eclipse-temurin-21 AS builder
WORKDIR /app
COPY . .
RUN mvn clean package -DskipTests

FROM eclipse-temurin:21-jre-alpine
COPY --from=builder /app/target/dns-api.jar /app/dns-api.jar
EXPOSE 8080
ENTRYPOINT ["java", "-jar", "/app/dns-api.jar"]
```
Usar una imagen JRE (no JDK) en la etapa final reduce considerablemente el tamaño de la imagen, ya que no se necesita el compilador ni las herramientas de build en producción.

**Integración en docker-compose.yml con healthcheck:**
```yaml
services:
  dns-api:
    build: ./dns-api
    environment:
      - REMOTE_DNS_SERVER=${REMOTE_DNS_SERVER}
      - REMOTE_DNS_PORT=${REMOTE_DNS_PORT}
      - UDP_TIMEOUT_MS=${UDP_TIMEOUT_MS}
      - DB_URL=${DB_URL}
      - DB_API_KEY=${DB_API_KEY}
    healthcheck:
      test: ["CMD", "curl", "-f", "http://localhost:8080/health"]
      interval: 30s
      timeout: 5s
      retries: 3
      start_period: 20s
```
Todas las configuraciones (servidor DNS remoto, timeouts, credenciales de la base de datos) se inyectan por variables de entorno — ninguna debe quedar quemada en el código, tal como exige el enunciado. El healthcheck permite que otros servicios (como el DNS Interceptor) usen `depends_on: condition: service_healthy` para no arrancar antes de que la DNS API esté lista.

> Nota: si la imagen base es Alpine, `curl` no viene incluido por defecto — hay que instalarlo explícitamente en el Dockerfile, o el healthcheck fallará silenciosamente aunque el servicio esté funcionando bien.

---

## 5. Referencias consultadas

- Documentación oficial de Docker Compose — healthchecks y `depends_on` con `condition: service_healthy`.
- Repositorio `dnsjava` (implementación DNS en Java, thread-safe, soporta EDNS0 y TSIG).
- Artículos sobre implementación de servidores DNS en Rust con `tokio` + `trust-dns-proto`.
- Guías de contenedorización de aplicaciones Rust y Java con builds multi-stage.
- Comparativas de frameworks REST ligeros en Java (Javalin) frente a Spring Boot.

*(Este documento fue creado por el estudiante Kevin Espinoza Barrantes utilizando Claude Sonnet 5 como medio de la escritura final despues de la busqueda de fuentes y el uso de esta misma IA para entender todo el material presentado.)*
