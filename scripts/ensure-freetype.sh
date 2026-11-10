#!/usr/bin/env bash
set -euo pipefail

# Provision the FreeType source the HUD's text is rasterized with.
#
# Dear ImGui's own rasterizer (stb_truetype) does no hinting, so small UI text
# comes out thin and soft; ImGui's FreeType loader hints it to the pixel grid.
# FreeType is built from source with every optional dependency (zlib, PNG,
# HarfBuzz, Brotli, bzip2) switched off, so it needs nothing from the system on
# any platform. Pinned with the checksum of its source archive in
# scripts/freetype-version.txt, and kept in .cache beside Dear ImGui.

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
pin_file="${repo_root}/scripts/freetype-version.txt"
read -r version checksum < <(tr -d '\r' < "${pin_file}")
root="${ZOAL_ATC_FREETYPE_SOURCE:-${repo_root}/.cache/freetype-${version}}"

if [ -f "${root}/include/ft2build.h" ]; then
  exit 0
fi

work="$(mktemp -d)"
trap 'rm -rf "${work}"' EXIT
archive="${work}/freetype.tar.gz"
echo "Downloading FreeType ${version}..."
curl -fsSL -o "${archive}" \
  "https://github.com/freetype/freetype/archive/refs/tags/${version}.tar.gz"

actual="$(shasum -a 256 "${archive}" 2>/dev/null || sha256sum "${archive}")"
actual="${actual%% *}"
if [ "${actual}" != "${checksum}" ]; then
  echo "FreeType ${version} archive checksum is ${actual}, want ${checksum}" >&2
  exit 1
fi

mkdir -p "${work}/src"
tar -xzf "${archive}" -C "${work}/src"
mkdir -p "$(dirname "${root}")"
rm -rf "${root}"
mv "${work}/src/"freetype-* "${root}"
echo "FreeType ${version} at ${root}"
