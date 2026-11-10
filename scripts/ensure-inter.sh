#!/usr/bin/env bash
set -euo pipefail

# Provision Inter, the typeface the radio HUD is set in.
#
# The React HUD was set in the platform's UI font (San Francisco on a Mac), and
# browsers kern; Dear ImGui does not kern at all, and Roboto -- X-Plane's own UI
# face -- leans on its kerning, so set by ImGui it reads unevenly. Inter is
# drawn for screens at 11-16px and spaces well unkerned. It ships inside the
# plugin folder (fonts/), under the SIL Open Font License, so every platform
# draws the same HUD. Only the three weights the HUD uses are kept.
#
# Pinned with the checksum of the release archive in scripts/inter-version.txt
# and kept in .cache. Extracted with cmake, which every build host has, rather
# than unzip, which a Windows runner may not.

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
pin_file="${repo_root}/scripts/inter-version.txt"
read -r version checksum < <(tr -d '\r' < "${pin_file}")
root="${ZOAL_ATC_INTER:-${repo_root}/.cache/inter-${version}}"
weights=(Inter-Regular.ttf Inter-SemiBold.ttf Inter-Bold.ttf)

complete=1
for weight in "${weights[@]}" LICENSE.txt; do
  [ -f "${root}/${weight}" ] || complete=0
done
if [ "${complete}" -eq 1 ]; then
  exit 0
fi

work="$(mktemp -d)"
trap 'rm -rf "${work}"' EXIT
archive="${work}/inter.zip"
echo "Downloading Inter ${version}..."
curl -fsSL -o "${archive}" \
  "https://github.com/rsms/inter/releases/download/${version}/Inter-${version#v}.zip"

actual="$(shasum -a 256 "${archive}" 2>/dev/null || sha256sum "${archive}")"
actual="${actual%% *}"
if [ "${actual}" != "${checksum}" ]; then
  echo "Inter ${version} archive checksum is ${actual}, want ${checksum}" >&2
  exit 1
fi

mkdir -p "${work}/src"
(cd "${work}/src" && cmake -E tar xf "${archive}")
mkdir -p "${root}"
for weight in "${weights[@]}"; do
  cp "${work}/src/extras/ttf/${weight}" "${root}/${weight}"
done
cp "${work}/src/LICENSE.txt" "${root}/LICENSE.txt"
echo "Inter ${version} at ${root}"
