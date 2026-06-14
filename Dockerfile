FROM ubuntu:24.04 AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake ninja-build ccache \
    pkg-config \
    libssl-dev zlib1g-dev libpng-dev libjpeg-dev \
    libxml2-dev libcurl4-openssl-dev \
    libmysqlclient-dev libpq-dev \
    liblua5.2-dev \
    libgif-dev libfreetype6-dev \
    git ca-certificates netcat-openbsd \
    libxdp1 libbpf1 libnl-3-200 libnl-route-3-200 libnuma1 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

# Run the build.
# - A failed compile MUST fail the image (no `|| echo` — that shipped empty images).
# - The ryzom-modernize preset's binaryDir is build/ryzom (see ryzom/CMakePresets.json),
#   so build that tree, not build/.
# - Build Release: the Debug config trips NeL nlassert/BOMB_IF landmines at runtime.
# - MSQUIC disabled: the private shard communicates via go-proxy (WebSocket→UDP);
#   MSQUIC runtime lib is not packaged, and the feature is not needed here.
RUN cmake --preset ryzom-modernize -DWITH_MSQUIC=OFF && \
    cmake --build build/ryzom --config Release --parallel "$(nproc)"

# Runtime stage
FROM ubuntu:24.04 AS runtime

RUN apt-get update && apt-get install -y --no-install-recommends \
    libssl3 libxml2 libcurl4 libmysqlclient21 libpq5 liblua5.2-0 \
    libgif7 libfreetype6 netcat-openbsd \
    && rm -rf /var/lib/apt/lists/*

RUN mkdir -p /app/bin /app/lib /app/etc
WORKDIR /app/bin

COPY --from=builder /src/build/ryzom/lib/Release/* /app/lib/
COPY --from=builder /src/build/ryzom/bin/Release/ /app/bin/
COPY --from=builder /src/docker/run_shard_container.sh /app/bin/

ENV PATH="/app/bin:${PATH}"
ENV LD_LIBRARY_PATH="/app/lib:${LD_LIBRARY_PATH}"

# --- Service targets ---

FROM runtime AS naming_service
CMD ["ryzom_naming_service", "--noBg"]

FROM runtime AS login_service
CMD ["login_service", "--noBg"]

FROM runtime AS shard_runtime
CMD ["/app/bin/run_shard_container.sh"]
