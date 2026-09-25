# Resumen de pruebas del DNS API

## 1. Objetivo

Validar que el proyecto `DNS API` funciona correctamente sin Docker, usando:

- Java 21 / JDK
- Maven
- Firebase Emulator Suite
- API REST local

Se verificaron tanto las pruebas unitarias como las pruebas de integración del proyecto.

---

## 2. Requisitos previos

### Instalar Java 21 y Maven

Se instaló Java 21 y Maven manualmente en Windows. La validación fue:

```powershell
java -version
mvn -version
```

Resultado verificado:

- Java 21 instalado correctamente
- Maven 3.9.9 funcionando correctamente

### Instalar Firebase CLI

```powershell
npm install -g firebase-tools
```

Esto permite levantar los emuladores de Auth y Database de Firebase.

---

## 3. Pruebas unitarias

Comando ejecutado:

```powershell
cd "C:\dev\IC7602\2026-02-2023055841-IC7602-P1\DNS API"
mvn test
```

Resultado verificado:

```text
[INFO] Running cr.ac.tec.ic7602.dnsapi.firebase.FirebaseDnsExistsCheckerTest
[INFO] Tests run: 5, Failures: 0, Errors: 0, Skipped: 0
[INFO] BUILD SUCCESS
```

### Qué cubren estos 5 tests

1. Registro existente
2. Registro inexistente
3. Registro deshabilitado (`enabled: false`)
4. Normalización del dominio y construcción de la consulta
5. Error 401 / rechazo de autenticación

---

## 4. Preparar Firebase Emulator

Comando ejecutado:

```powershell
cd "C:\dev\IC7602\2026-02-2023055841-IC7602-P1\firebase"
firebase emulators:start
```

Resultado verificado:

- Auth emulator: `127.0.0.1:9099`
- Database emulator: `127.0.0.1:9000`
- UI: `http://127.0.0.1:4000`

---

## 5. Configuración local del `.env`

Se usó el archivo de ejemplo y se dejó la configuración local con `127.0.0.1`:

```env
DB_URL=http://127.0.0.1:9000
FIREBASE_AUTH_URL=http://127.0.0.1:9099
FIREBASE_EMAIL=dns-api@test.com
FIREBASE_PASSWORD=secreto123
```

Esto es necesario porque fuera de Docker no existe `host.docker.internal`.

---

## 6. Compilar y arrancar la API sin Docker

Terminal de la API:

```powershell
$env:Path = "C:\Users\kvnes\Tools\apache-maven-3.9.9\bin;$env:Path"
cd "C:\dev\IC7602\2026-02-2023055841-IC7602-P1\DNS API"

Get-Content .env | Where-Object { $_ -and $_ -notmatch '^#' } | ForEach-Object {
    $k, $v = $_ -split '=', 2
    Set-Item "env:$k" $v
}

mvn clean package -DskipTests
java -jar .\target\dns-api.jar
```

Resultado verificado:

```text
{"status":"UP"}
```

Cuando se hace `curl http://localhost:8080/health`, la API responde correctamente.

---

## 7. Pruebas de integración

Se ejecutó el script:

```bash
cd "/c/dev/IC7602/2026-02-2023055841-IC7602-P1/DNS API"
bash ./tests/pruebas_integracion.sh
```

Resultado verificado:

```text
== Resultado: 15 correctas, 0 fallidas ==
```

### Casos cubiertos por la prueba de integración

- Preparación del emulador
- Carga de registros de prueba
- Creación del usuario del API en Firebase Auth
- Login directo a Firebase Auth
- Consulta directa a Realtime Database
- GET /health
- POST /api/exists para dominio existente
- POST /api/exists para dominio en mayúsculas con punto final
- POST /api/exists para dominio inexistente
- POST /api/exists para registro `enabled: false`
- POST /api/exists sin `domain`
- POST /api/exists con JSON inválido
- GET /api/records/{hostname}
- GET /api/records/{hostname} inexistente
- POST /api/dns_resolver con consulta válida
- POST /api/dns_resolver con BASE64 inválido
- POST /api/dns_resolver sin `data`
- 50 peticiones simultáneas con 200 OK

---

## 8. Resultado final

La API se valida correctamente en estas condiciones:

- sin Docker,
- con Firebase Emulator local,
- con Maven y Java instalados localmente,
- con pruebas unitarias y de integración pasando.

La verificación final fue exitosa:

```text
Tests run: 5, Failures: 0, Errors: 0, Skipped: 0
== Resultado: 15 correctas, 0 fallidas ==
```

---

## 9. Cómo repetir todo

### Terminal 1: Firebase Emulator

```powershell
cd "C:\dev\IC7602\2026-02-2023055841-IC7602-P1\firebase"
firebase emulators:start
```

### Terminal 2: Arrancar la API

```powershell
$env:Path = "C:\Users\kvnes\Tools\apache-maven-3.9.9\bin;$env:Path"
cd "C:\dev\IC7602\2026-02-2023055841-IC7602-P1\DNS API"

Get-Content .env | Where-Object { $_ -and $_ -notmatch '^#' } | ForEach-Object {
    $k, $v = $_ -split '=', 2
    Set-Item "env:$k" $v
}

mvn clean package -DskipTests
java -jar .\target\dns-api.jar
```

### Terminal 3: Pruebas rápidas

```powershell
curl.exe http://localhost:8080/health
curl.exe -X POST http://localhost:8080/api/exists -H "Content-Type: application/json" -d '{"domain":"local-tcp.test"}'
curl.exe http://localhost:8080/api/records/local-tcp.test
```

### Terminal 4: Script de integracion

```bash
cd "/c/dev/IC7602/2026-02-2023055841-IC7602-P1/DNS API"
bash ./tests/pruebas_integracion.sh
```

---

## 10. Nota final

El proyecto quedó validado y funcionando sin Docker, con las pruebas relevantes ejecutadas y aprobadas. La verificación se hizo con evidencia real de la salida del sistema.
