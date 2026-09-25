# Firebase emulators for the Health Checker

The Health Checker uses the Authentication and Realtime Database emulators
configured in `firebase.json`.

## Start the emulators

From this directory, run:

```powershell
firebase emulators:start --import ..\firebase-data --export-on-exit ..\firebase-data
```

The expected endpoints are:

- Authentication: `http://127.0.0.1:9099`
- Realtime Database: `http://127.0.0.1:9000`
- Emulator UI: the URL printed by the Firebase CLI

Create an email/password user in the Authentication emulator. Set
`CHECKER_ID` in `HealthChecker/.env` to that user's Firebase UID. The database
rules intentionally allow a checker to write only its own registration and
result nodes.

## Run the Health Checker container

Copy `HealthChecker/.env.example` to `HealthChecker/.env`, fill every required
value, and keep the emulator URLs set to `host.docker.internal`. Then run:

```powershell
docker compose -f ..\HealthChecker\compose.yaml up --build
```

The checker registers itself, downloads enabled DNS targets, executes each
check according to `intervalSeconds`, and publishes results under:

```text
/healthResults/{recordId}/{targetId}/{checkerId}
```

When a monitored service runs on the host, its target address must be
`host.docker.internal`, not `127.0.0.1`. A target using `127.0.0.1` refers to
the Health Checker container itself.

Stop the service with `Ctrl+C` or:

```powershell
docker compose -f ..\HealthChecker\compose.yaml down
```
