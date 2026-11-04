# BASE_IMAGE can point at a registry mirror if Docker Hub is unavailable.
ARG BASE_IMAGE=debian:bookworm-slim

# Build stage: compile and test PulseDB.
FROM ${BASE_IMAGE} AS build
RUN apt-get update \
 && apt-get install -y --no-install-recommends gcc libc6-dev make \
 && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY makefile ./
COPY src ./src
COPY tools ./tools
COPY tests ./tests
RUN make all && make test

# Runtime stage: just the two binaries on a slim base, running as non-root.
FROM ${BASE_IMAGE}
RUN useradd --system --uid 10001 --home-dir /data pulsedb \
 && mkdir -p /data && chown pulsedb /data
COPY --from=build /src/pulsedb /src/pulsedb-cli /usr/local/bin/
USER pulsedb
WORKDIR /data
VOLUME /data
EXPOSE 6379
# Inside a container the server must listen on all interfaces to be
# reachable through the published port.
ENTRYPOINT ["pulsedb", "--bind", "0.0.0.0", "--dir", "/data"]
