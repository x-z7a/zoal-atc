# Debian bookworm with curl and the usual build tools already in it. The image
# is Node's for history -- the panel was once a web app -- and nothing in the
# build uses Node any more.
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
