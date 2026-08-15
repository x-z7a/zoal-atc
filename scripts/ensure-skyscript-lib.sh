#!/usr/bin/env bash
set -euo pipefail

# Provision the Skyscript library bundle the plugin links against.
#
# Skyscript is not optional. The in-sim panel is the ATC GUI (phase 23), so
# every plugin build consumes this bundle and there is no Skyscript-free path to
# fall back to. A missing or stale bundle is a build failure, not a downgrade.
#
# The bundle comes from a pinned GitHub *release* rather than the newest CI
# artifact, so a build is reproducible and the version moves when someone
# decides it should. The pin lives in scripts/skyscript-version.txt, which is
# the one place to edit to take a new Skyscript and the value CI keys its cache
# on. Set ZOAL_ATC_SKYSCRIPT_VERSION=latest to check what the newest release is,
# or to a tag to try one without editing the pin.
#
# CEF is deliberately absent. Since Skyscript v0.5.0 the library loads the CEF
# runtime that ships with X-Plane -- on macOS the dylib references
# @executable_path/../Frameworks/Chromium Embedded Framework.framework -- so no
# CEF is downloaded, validated, or packaged beside the plugin. Requiring it here
# is what used to make this script fail against a bundle that was correct.

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
root="${ZOAL_ATC_SKYSCRIPT_ROOT:-${repo_root}/.cache/skyscript/SkyScript-lib}"
repo="${ZOAL_ATC_SKYSCRIPT_REPO:-x-z7a/SkyScript}"
pin_file="${repo_root}/scripts/skyscript-version.txt"
if [ -z "${ZOAL_ATC_SKYSCRIPT_VERSION:-}" ] && [ ! -f "${pin_file}" ]; then
  echo "Missing Skyscript version pin: ${pin_file}" >&2
  exit 1
fi
version="${ZOAL_ATC_SKYSCRIPT_VERSION:-$(tr -d '[:space:]' < "${pin_file}")}"
platform="${ZOAL_ATC_SKYSCRIPT_PLATFORM:-}"

if [ -z "${platform}" ]; then
  case "${OS:-}:$(uname -s)" in
    Windows_NT:*|*:MINGW*|*:MSYS*|*:CYGWIN*) platform="win_x64" ;;
    *:Darwin) platform="mac_x64" ;;
    *:Linux) platform="lin_x64" ;;
    *)
      echo "Unsupported platform for Skyscript bootstrap: $(uname -s)" >&2
      exit 1
      ;;
  esac
fi

case "${platform}" in
  mac_x64) library_name="libSkyScriptLib.dylib" ;;
  lin_x64) library_name="libSkyScriptLib.so" ;;
  win_x64) library_name="SkyScriptLib.dll" ;;
  *)
    echo "Unsupported Skyscript platform: ${platform}" >&2
    exit 1
    ;;
esac

stamp="${root}/SKYSCRIPT_VERSION"

# The stamp is what makes bumping the pin take effect. Validating only that the
# files exist would leave a cached older bundle in place forever, and the build
# would keep linking a version nobody asked for.
validate_platform() {
  local library="${root}/lib/${platform}/${library_name}"
  [ -f "${root}/include/skyscript_c.h" ] || return 1
  [ -f "${root}/assets/icons/x-circle.svg" ] || return 1
  [ -f "${library}" ] || return 1
  [ -f "${stamp}" ] || return 1
  [ "$(cat "${stamp}")" = "${version}" ] || return 1

  # Skyscript can also be built to load a CEF runtime shipped beside the plugin
  # (SKYSCRIPT_PLUGIN_LOCAL_CEF). We do not package one, so a library built that
  # way would resolve nothing at load time. Catch it here rather than in the sim.
  if [ "${platform}" = "mac_x64" ] && command -v otool >/dev/null 2>&1; then
    if otool -L "${library}" | grep -q "@loader_path/Chromium Embedded Framework.framework"; then
      return 1
    fi
  fi
}

if [ "${version}" != "latest" ] && validate_platform; then
  echo "Skyscript ${version} ${platform} ready at ${root}"
  exit 0
fi

if ! command -v gh >/dev/null 2>&1; then
  echo "gh is required to download the Skyscript release. Install gh, or set ZOAL_ATC_SKYSCRIPT_ROOT to a prepared SkyScript-lib bundle." >&2
  exit 1
fi

if [ "${version}" = "latest" ]; then
  version="$(gh release view --repo "${repo}" --json tagName --jq '.tagName')"
  if [ -z "${version}" ] || [ "${version}" = "null" ]; then
    echo "Could not resolve the latest Skyscript release on ${repo}" >&2
    exit 1
  fi
  echo "Resolved Skyscript latest to ${version}"
  if validate_platform; then
    echo "Skyscript ${version} ${platform} ready at ${root}"
    exit 0
  fi
fi

work_dir="$(mktemp -d "${TMPDIR:-/tmp}/zoal-atc-skyscript.XXXXXX")"
cleanup() {
  cmake -E rm -rf "${work_dir}"
}
trap cleanup EXIT

bundle_dir="${work_dir}/bundle"
cmake -E make_directory "${bundle_dir}"

echo "Downloading Skyscript ${version} library bundle from ${repo}"
gh release download "${version}" \
  --repo "${repo}" \
  --pattern 'SkyScript-lib-*.zip' \
  --dir "${bundle_dir}"

bundle_zip="$(find "${bundle_dir}" -maxdepth 1 -type f -name 'SkyScript-lib-*.zip' | head -n 1)"
if [ -z "${bundle_zip}" ]; then
  echo "Skyscript release ${version} has no SkyScript-lib-*.zip asset" >&2
  exit 1
fi

# cmake -E tar reads zip and tar.gz alike, which keeps this working on the
# Windows runner where unzip is not guaranteed.
extract_dir="${work_dir}/extracted"
cmake -E make_directory "${extract_dir}"
(cd "${extract_dir}" && cmake -E tar xf "${bundle_zip}")

lib_root="$(dirname "$(find "${extract_dir}" -type f -name 'skyscript_c.h' | head -n 1)")/.."
# Assets ship with the plugin at runtime but are not in the library bundle yet,
# so fall back to the source tree at the same tag. Preferring the bundle means
# this needs no change once upstream includes them.
assets_source="${lib_root}/assets"
license_source="${lib_root}/LICENSE"
if [ ! -d "${assets_source}" ]; then
  echo "Fetching Skyscript ${version} assets from source (not in the library bundle)"
  source_archive="${work_dir}/source.tar.gz"
  gh api "repos/${repo}/tarball/${version}" > "${source_archive}"
  source_dir="${work_dir}/source"
  cmake -E make_directory "${source_dir}"
  (cd "${source_dir}" && cmake -E tar xzf "${source_archive}")
  assets_source="$(find "${source_dir}" -maxdepth 2 -type d -name 'assets' | head -n 1)"
  license_source="$(find "${source_dir}" -maxdepth 2 -type f -name 'LICENSE' | head -n 1)"
fi

if [ -z "${assets_source}" ] || [ ! -d "${assets_source}" ]; then
  echo "Could not obtain Skyscript ${version} assets" >&2
  exit 1
fi

# A version bump invalidates every platform at once, so the old tree goes.
# Anything less would leave one platform's library behind at a version nobody
# pinned, which is the sort of mismatch that only shows up in the sim.
if [ -f "${stamp}" ] && [ "$(cat "${stamp}")" != "${version}" ]; then
  cmake -E rm -rf "${root}"
fi

cmake -E make_directory "${root}/include" "${root}/assets"
cmake -E copy "$(find "${extract_dir}" -type f -name 'skyscript_c.h' | head -n 1)" "${root}/include/skyscript_c.h"
cmake -E copy_directory "${assets_source}" "${root}/assets"

if [ -n "${license_source}" ] && [ -f "${license_source}" ]; then
  cmake -E copy "${license_source}" "${root}/LICENSE"
fi

# One bundle carries all three platforms, so install all of them. The
# cross-platform release build packages mac, Windows and Linux from one machine
# and would otherwise have to re-download the same zip for each.
for bundled_platform in mac_x64 lin_x64 win_x64; do
  case "${bundled_platform}" in
    mac_x64) bundled_library="libSkyScriptLib.dylib" ;;
    lin_x64) bundled_library="libSkyScriptLib.so" ;;
    win_x64) bundled_library="SkyScriptLib.dll" ;;
  esac
  bundled_source="$(find "${extract_dir}" -type f -path "*${bundled_platform}*" -name "${bundled_library}" | head -n 1)"
  if [ -z "${bundled_source}" ]; then
    continue
  fi
  cmake -E make_directory "${root}/lib/${bundled_platform}"
  cmake -E copy "${bundled_source}" "${root}/lib/${bundled_platform}/${bundled_library}"
  if [ "${bundled_platform}" = "win_x64" ]; then
    import_library="$(find "${extract_dir}" -type f -name 'SkyScriptLib.lib' | head -n 1)"
    if [ -n "${import_library}" ]; then
      cmake -E copy "${import_library}" "${root}/lib/${bundled_platform}/SkyScriptLib.lib"
    fi
  fi
done

if [ ! -f "${root}/lib/${platform}/${library_name}" ]; then
  echo "Skyscript ${version} bundle does not contain ${library_name} for ${platform}" >&2
  exit 1
fi

echo "${version}" > "${stamp}"

if ! validate_platform; then
  echo "Skyscript ${version} ${platform} bootstrap completed but validation failed under ${root}" >&2
  exit 1
fi

echo "Skyscript ${version} ${platform} ready at ${root}"
