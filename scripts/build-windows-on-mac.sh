#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
output_dir="${1:-${repo_root}/dist/zoal-atc-windows-x64}"
plugin_build_dir="${repo_root}/build-plugin-windows"
sdk_dir="${repo_root}/sdk"
miniaudio_dir="${repo_root}/vendor/miniaudio"
skyscript_root="${ZOAL_ATC_SKYSCRIPT_ROOT:-${repo_root}/.cache/skyscript/SkyScript-lib}"

if [ "$(uname -s)" != "Darwin" ]; then
  echo "build-windows-on-mac.sh must be run on macOS" >&2
  exit 1
fi

required_commands=(
  cmake
  x86_64-w64-mingw32-gcc
  x86_64-w64-mingw32-g++
  x86_64-w64-mingw32-windres
)

for command_name in "${required_commands[@]}"; do
  if ! command -v "${command_name}" >/dev/null 2>&1; then
    echo "Missing required command: ${command_name}" >&2
    if [[ "${command_name}" == x86_64-w64-mingw32-* ]]; then
      echo "Install the Windows cross-toolchain with: brew install mingw-w64" >&2
    fi
    exit 1
  fi
done

echo "==> Preparing the X-Plane SDK and miniaudio"
cmake -DATC_XPLANE_SDK="${sdk_dir}" -P "${repo_root}/cmake/SetupXPlaneSDK.cmake"
if [ ! -f "${miniaudio_dir}/miniaudio.h" ]; then
  cmake -DMINIAUDIO_DIR="${miniaudio_dir}" -P "${repo_root}/cmake/SetupMiniaudio.cmake"
else
  echo "-- miniaudio already present at ${miniaudio_dir}/miniaudio.h"
fi

echo "==> Ensuring the Skyscript library for win_x64"
ZOAL_ATC_SKYSCRIPT_ROOT="${skyscript_root}" \
  ZOAL_ATC_SKYSCRIPT_PLATFORM=win_x64 \
  "${repo_root}/scripts/ensure-skyscript-lib.sh"

echo "==> Cross-building the Windows x64 X-Plane plugin"
cmake \
  -S "${repo_root}" \
  -B "${plugin_build_dir}" \
  -DCMAKE_SYSTEM_NAME=Windows \
  -DCMAKE_SYSTEM_PROCESSOR=x86_64 \
  -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-g++ \
  -DCMAKE_RC_COMPILER=x86_64-w64-mingw32-windres \
  -DCMAKE_BUILD_TYPE=Release \
  -DZOAL_ATC_BUILD_PLUGIN=ON \
  -DZOAL_ATC_BUILD_TESTS=OFF \
  -DATC_XPLANE_SDK="${sdk_dir}" \
  -DATC_MINIAUDIO_INCLUDE_DIR="${miniaudio_dir}" \
  -DZOAL_ATC_SKYSCRIPT_ROOT="${skyscript_root}"
cmake --build "${plugin_build_dir}" --parallel --target zoal_atc

plugin_output="${plugin_build_dir}/zoal_atc.xpl"

if [ ! -f "${plugin_output}" ]; then
  echo "Plugin build did not produce ${plugin_output}" >&2
  exit 1
fi

echo "==> Collecting Windows artifacts"
cmake -E rm -rf "${output_dir}"
cmake -E make_directory "${output_dir}/win_x64"
cmake -E copy "${plugin_output}" "${output_dir}/win_x64/zoal-atc.xpl"

echo "Windows x64 build complete: ${output_dir}"
