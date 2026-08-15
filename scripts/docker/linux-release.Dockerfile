# Node is the base rather than an apt package because the panel's toolchain
# (esbuild, vitest) tracks the same Node 20 CI uses, and bookworm ships 18.
FROM node:20-bookworm

RUN apt-get update \
    && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        build-essential \
        ca-certificates \
        cmake \
        git \
        rsync \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /work
