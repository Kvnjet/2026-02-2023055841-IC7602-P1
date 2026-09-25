# --- compilar ---
FROM rust:bookworm AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    libpcap-dev \
    libssl-dev \
    pkg-config \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /usr/src/app

COPY Cargo.toml Cargo.lock ./

# para que no compile las dependencias cada cambio
RUN mkdir src && echo "fn main() {}" > src/main.rs
RUN cargo build --release
RUN rm -rf src target/release/DNS target/release/deps/DNS*

COPY src ./src
RUN cargo build --release


FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
    libpcap0.8 \
    libssl3 \
    ca-certificates \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY --from=builder /usr/src/app/target/release/DNS /usr/local/bin/dns-interceptor

EXPOSE 9090

ENTRYPOINT ["dns-interceptor"]
