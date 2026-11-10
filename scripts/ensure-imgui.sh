#!/usr/bin/env bash
set -euo pipefail

# Provision the Dear ImGui source the plugin draws its HUD with.
#
# X-Plane 12.4.4 renders ImGui's output natively (XPLMPanelGraphics, "IMGUI
# HELPERS") but does not ship ImGui itself: the plugin compiles its own. Only
# the core is used -- X-Plane is the renderer and the plugin is the platform, so
# none of ImGui's backends are built. Pinned with the checksum of its source
# archive in scripts/imgui-version.txt, and kept in .cache beside whisper.cpp.

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
pin_file="${repo_root}/scripts/imgui-version.txt"
read -r version checksum < <(tr -d '\r' < "${pin_file}")
root="${ZOAL_ATC_IMGUI_SOURCE:-${repo_root}/.cache/imgui-${version}}"

if [ -f "${root}/imgui.h" ]; then
  exit 0
fi

work="$(mktemp -d)"
trap 'rm -rf "${work}"' EXIT
archive="${work}/imgui.tar.gz"
echo "Downloading Dear ImGui ${version}..."
curl -fsSL -o "${archive}" \
  "https://github.com/ocornut/imgui/archive/refs/tags/${version}.tar.gz"

actual="$(shasum -a 256 "${archive}" 2>/dev/null || sha256sum "${archive}")"
actual="${actual%% *}"
if [ "${actual}" != "${checksum}" ]; then
  echo "Dear ImGui ${version} archive checksum is ${actual}, want ${checksum}" >&2
  exit 1
fi

mkdir -p "${work}/src"
tar -xzf "${archive}" -C "${work}/src"
mkdir -p "$(dirname "${root}")"
rm -rf "${root}"
mv "${work}/src/"imgui-* "${root}"
echo "Dear ImGui ${version} at ${root}"
