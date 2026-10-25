#!/usr/bin/env bash
set -euo pipefail

# Provision the whisper.cpp source the plugin builds its local transcriber from.
#
# The plugin transcribes push-to-talk in the cockpit, so the words reach the
# console in place of the recording. It builds the same whisper.cpp release the
# console does, pinned with the checksum of its source archive in
# scripts/whisper-version.txt, so a turn is heard the same way whichever end
# transcribes it. The source is a download, not ours to commit, and lives in
# .cache beside the Skyscript bundle.

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
pin_file="${repo_root}/scripts/whisper-version.txt"
read -r version checksum < <(tr -d '\r' < "${pin_file}")
root="${ZOAL_ATC_WHISPER_SOURCE:-${repo_root}/.cache/whisper.cpp-${version}}"

if [ -f "${root}/include/whisper.h" ]; then
  exit 0
fi

work="$(mktemp -d)"
trap 'rm -rf "${work}"' EXIT
archive="${work}/whisper.tar.gz"
echo "Downloading whisper.cpp ${version}..."
curl -fsSL -o "${archive}" \
  "https://github.com/ggml-org/whisper.cpp/archive/refs/tags/${version}.tar.gz"

actual="$(shasum -a 256 "${archive}" 2>/dev/null || sha256sum "${archive}")"
actual="${actual%% *}"
if [ "${actual}" != "${checksum}" ]; then
  echo "whisper.cpp ${version} archive checksum is ${actual}, want ${checksum}" >&2
  exit 1
fi

mkdir -p "${work}/src"
tar -xzf "${archive}" -C "${work}/src"
mkdir -p "$(dirname "${root}")"
rm -rf "${root}"
mv "${work}/src/"whisper.cpp-* "${root}"
echo "whisper.cpp ${version} at ${root}"
