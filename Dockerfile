# Storm Island backend + dedicated game server.
#
# One container runs the HTTP backend (accounts, lockers, shop, matchmaking, admin panel)
# and the backend spawns game-server processes inside the same container on demand.
#
#   docker build -t stormisland-backend .
#   docker run -d -p 8080:8080 -p 7777-7786:7777-7786/udp \
#     -e STORM_PUBLIC_HOST=<your server's public IP or hostname> \
#     -e STORM_ADMIN_PASSWORD=<choose one> -v stormisland-data:/data stormisland-backend
#
# See docs/HOSTING.md for details.

# Base image; override with --build-arg BASE_IMAGE=... (e.g. a registry mirror).
ARG BASE_IMAGE=debian:bookworm-slim

# ---------------------------------------------------------------- build
FROM ${BASE_IMAGE} AS build
RUN apt-get update \
 && apt-get install -y --no-install-recommends build-essential cmake \
 && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY CMakeLists.txt ./
COPY shared ./shared
COPY server ./server
COPY backend ./backend
COPY tests ./tests
COPY third_party ./third_party
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DSTORM_BUILD_CLIENT=OFF \
 && cmake --build build --parallel \
 && ctest --test-dir build --output-on-failure \
 && strip build/bin/stormisland-backend build/bin/stormisland-server

# ---------------------------------------------------------------- runtime
FROM ${BASE_IMAGE}
RUN apt-get update \
 && apt-get install -y --no-install-recommends tini curl ca-certificates \
 && rm -rf /var/lib/apt/lists/* \
 && useradd --system --create-home --home-dir /app --shell /usr/sbin/nologin storm \
 && mkdir -p /data && chown storm:storm /data
WORKDIR /app
COPY --from=build /src/build/bin/stormisland-backend /src/build/bin/stormisland-server /app/
COPY --from=build /src/build/bin/web /app/web

# All settings are environment variables (see docs/HOSTING.md). STORM_PUBLIC_HOST must be
# the address players' clients can reach, otherwise matches only work on this machine.
ENV STORM_HTTP_PORT=8080 \
    STORM_DB=/data/stormisland.db \
    STORM_PORTS=7777-7786 \
    STORM_PUBLIC_HOST=127.0.0.1

USER storm
VOLUME ["/data"]
EXPOSE 8080/tcp
EXPOSE 7777-7786/udp

HEALTHCHECK --interval=30s --timeout=5s --start-period=10s --retries=3 \
  CMD curl -fsS "http://127.0.0.1:${STORM_HTTP_PORT}/health" || exit 1

# tini reaps finished game-server processes and forwards SIGTERM for a clean shutdown.
ENTRYPOINT ["/usr/bin/tini", "--", "/app/stormisland-backend"]
