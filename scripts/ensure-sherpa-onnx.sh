#!/usr/bin/env bash
set -euo pipefail

# Provision sherpa-onnx, which the plugin speaks the controller's words with.
#
# The plugin says each transmission in the voice of the position sending it,
# synthesised with Kokoro on the pilot's own machine. sherpa-onnx is the
# runtime: a static build of it and of ONNX Runtime, linked into the .xpl so
# nothing else has to ship. Its release archive carries the libraries but not
# the C header, so the header is fetched from the same tag. Both are pinned by
# checksum in scripts/sherpa-onnx-version.txt, and both are downloads rather
# than ours to commit, so they live in .cache beside whisper.cpp.

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
pin_file="${repo_root}/scripts/sherpa-onnx-version.txt"
read -r version flavour archive_sum header_sum < "${pin_file}"
root="${ZOAL_ATC_SHERPA_ONNX:-${repo_root}/.cache/sherpa-onnx-${version}-${flavour}}"

if [ -f "${root}/include/sherpa-onnx/c-api/c-api.h" ] && [ -f "${root}/lib/libsherpa-onnx-c-api.a" ]; then
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
echo "sherpa-onnx ${version} at ${root}"
