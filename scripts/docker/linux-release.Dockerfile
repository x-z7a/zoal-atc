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
        glslc \
        libvulkan-dev \
        rsync \
        spirv-headers \
    && rm -rf /var/lib/apt/lists/*

# Whisper's Vulkan backend (x-z7a/zoal-atc#66) needs newer Vulkan headers than
# bookworm's 1.3.239, which lack VK_EXT_layer_settings. These are the 1.3.275
# headers Ubuntu 24.04 ships, where the release workflow builds Linux: headers
# only, installed under /usr/local so they come before /usr/include. The
# loader stays Debian's; the plugin finds the pilot's own at run time.
ARG VULKAN_HEADERS_VERSION=1.3.275
ARG VULKAN_HEADERS_SHA256=7161da645dbd33fd4ea61eec08e0d77389a640010acbf4afc00234f84df9b314
RUN curl -fsSL -o /tmp/vulkan-headers.tar.gz \
        "https://github.com/KhronosGroup/Vulkan-Headers/archive/refs/tags/v${VULKAN_HEADERS_VERSION}.tar.gz" \
    && echo "${VULKAN_HEADERS_SHA256}  /tmp/vulkan-headers.tar.gz" | sha256sum -c - \
    && tar -xzf /tmp/vulkan-headers.tar.gz -C /tmp \
    && cmake -S "/tmp/Vulkan-Headers-${VULKAN_HEADERS_VERSION}" -B /tmp/vulkan-headers-build \
        -DCMAKE_INSTALL_PREFIX=/usr/local -DVULKAN_HEADERS_ENABLE_MODULE=OFF >/dev/null \
    && cmake --install /tmp/vulkan-headers-build >/dev/null \
    && rm -rf /tmp/vulkan-headers.tar.gz "/tmp/Vulkan-Headers-${VULKAN_HEADERS_VERSION}" /tmp/vulkan-headers-build

WORKDIR /work
