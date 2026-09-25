FROM debian:bookworm-slim AS build

RUN apt-get update \
    && apt-get install --yes --no-install-recommends \
        build-essential \
        libcjson-dev \
        libcurl4-openssl-dev \
        pkg-config \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build
COPY Makefile ./
COPY include ./include
COPY src ./src
COPY tests ./tests
RUN make test \
    && make

FROM debian:bookworm-slim

RUN apt-get update \
    && apt-get install --yes --no-install-recommends \
        ca-certificates \
        libcjson1 \
        libcurl4 \
    && rm -rf /var/lib/apt/lists/* \
    && useradd --system --uid 10001 --create-home healthchecker

COPY --from=build /build/bin/health-checker /usr/local/bin/health-checker

WORKDIR /app
USER healthchecker
STOPSIGNAL SIGTERM
HEALTHCHECK --interval=30s --timeout=3s --start-period=5s --retries=3 \
    CMD kill -0 1 || exit 1
ENTRYPOINT ["/usr/local/bin/health-checker"]
CMD [".env"]
