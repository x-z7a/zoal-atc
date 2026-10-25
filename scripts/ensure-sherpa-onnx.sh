#!/usr/bin/env bash
set -euo pipefail

# Provision sherpa-onnx, which the plugin speaks the controller's words with.
#
# The plugin says each transmission in the voice of the position sending it,
# synthesised with Kokoro on the pilot's own machine, on every platform: there
# is no other voice to fall back to. sherpa-onnx is the runtime. On macOS it is
# a static build linked into the .xpl; on Windows and Linux it is the published
# shared build of its C API and ONNX Runtime, shipped beside the .xpl, because
# the static builds there are MSVC's and not linkable from MinGW. The release
# archives carry libraries but not the C header, so the header is fetched from
# the same tag. Everything is pinned by checksum in
# scripts/sherpa-onnx-version.txt.
#
# Usage: ensure-sherpa-onnx.sh [flavour]   (default: this machine's)
# Prints nothing on success when already present.

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
pin_file="${repo_root}/scripts/sherpa-onnx-version.txt"

pin() { tr -d '\r' < "${pin_file}" | awk -v key="$1" '$1 == key { print $2 }'; }

version="$(pin version)"
header_sum="$(pin header)"
flavour="${1:-${ZOAL_ATC_SHERPA_FLAVOUR:-}}"
if [ -z "${flavour}" ]; then
  case "$(uname -s)-$(uname -m)" in
    Darwin-arm64) flavour=osx-arm64-static-lib ;;
    Linux-x86_64) flavour=linux-x64-shared-lib ;;
    MINGW*|MSYS*|CYGWIN*) flavour=win-x64-shared-MT-Release-lib ;;
    *) echo "no pinned sherpa-onnx build for $(uname -s)-$(uname -m)" >&2; exit 1 ;;
  esac
fi
archive_sum="$(pin "${flavour}")"
if [ -z "${archive_sum}" ]; then
  echo "no pin for sherpa-onnx flavour ${flavour} in ${pin_file}" >&2
  exit 1
fi
root="${ZOAL_ATC_SHERPA_ONNX:-${repo_root}/.cache/sherpa-onnx-${version}-${flavour}}"

if [ -f "${root}/include/sherpa-onnx/c-api/c-api.h" ] && [ -d "${root}/lib" ]; then
  exit 0
fi

sum() {
  local out
  out="$(shasum -a 256 "$1" 2>/dev/null || sha256sum "$1")"
  echo "${out%% *}"
}

work="$(mktemp -d)"
trap 'rm -rf "${work}"' EXIT
name="sherpa-onnx-${version}-${flavour}"
echo "Downloading ${name}..."
curl -fsSL -o "${work}/${name}.tar.bz2" \
  "https://github.com/k2-fsa/sherpa-onnx/releases/download/${version}/${name}.tar.bz2"
actual="$(sum "${work}/${name}.tar.bz2")"
if [ "${actual}" != "${archive_sum}" ]; then
  echo "${name} archive checksum is ${actual}, want ${archive_sum}" >&2
  exit 1
fi

curl -fsSL -o "${work}/c-api.h" \
  "https://raw.githubusercontent.com/k2-fsa/sherpa-onnx/${version}/sherpa-onnx/c-api/c-api.h"
actual="$(sum "${work}/c-api.h")"
if [ "${actual}" != "${header_sum}" ]; then
  echo "sherpa-onnx ${version} c-api.h checksum is ${actual}, want ${header_sum}" >&2
  exit 1
fi

mkdir -p "${work}/src"
tar -xjf "${work}/${name}.tar.bz2" -C "${work}/src"
mkdir -p "${work}/src/${name}/include/sherpa-onnx/c-api"
mv "${work}/c-api.h" "${work}/src/${name}/include/sherpa-onnx/c-api/c-api.h"
mkdir -p "$(dirname "${root}")"
rm -rf "${root}"
mv "${work}/src/${name}" "${root}"
echo "sherpa-onnx ${version} ${flavour} at ${root}"
