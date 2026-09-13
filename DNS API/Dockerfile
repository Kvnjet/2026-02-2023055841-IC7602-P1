# ---build---
FROM maven:3.9-eclipse-temurin-21 AS builder
WORKDIR /app

# dependencias
COPY pom.xml .
RUN mvn -B dependency:go-offline

COPY src ./src
RUN mvn -B clean package -DskipTests

# ---runtime---
FROM eclipse-temurin:21-jre-alpine
RUN apk add --no-cache curl
WORKDIR /app

COPY --from=builder /app/target/dns-api.jar ./dns-api.jar

EXPOSE 8080
ENTRYPOINT ["java", "-jar", "dns-api.jar"]
