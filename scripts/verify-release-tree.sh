#!/usr/bin/env bash
set -euo pipefail

# Verify an assembled release tree is complete.
#
# A release is four things that have to arrive together: the plugin, the
# Skyscript library it links, the assets that library loads, and the in-sim
# panel app. Miss any one and the failure surfaces inside X-Plane as a window
# that will not open, which is the most expensive place to discover it and the
# hardest to read.
#
# CEF is deliberately absent: since Skyscript v0.5.0 the library resolves it
# against X-Plane's own install, so a release that shipped one would be sending
# ~200MB that is never loaded. The Whisper model is absent for the same class of
# reason - the console is hosted, and nothing under the plugin folder ever read
# the model.

if [ "$#" -lt 1 ] || [ "$#" -gt 2 ]; then
  echo "usage: verify-release-tree.sh <release-dir> [platform-dir]" >&2
  exit 2
fi

release_dir="$1"
platform_dir="${2:-}"

if [ -z "${platform_dir}" ]; then
  case "${OS:-}:$(uname -s)" in
    Windows_NT:*|*:MINGW*|*:MSYS*|*:CYGWIN*) platform_dir="win_x64" ;;
    *:Darwin) platform_dir="mac_x64" ;;
    *:Linux) platform_dir="lin_x64" ;;
    *)
      echo "Unsupported platform: $(uname -s)" >&2
      exit 1
      ;;
  esac
fi

case "${platform_dir}" in
  mac_x64) skyscript_library="libSkyScriptLib.dylib" ;;
  lin_x64) skyscript_library="libSkyScriptLib.so" ;;
  win_x64) skyscript_library="SkyScriptLib.dll" ;;
  *)
    echo "Unsupported platform directory: ${platform_dir}" >&2
    exit 1
    ;;
esac

failures=0

require_file() {
  if [ ! -f "${release_dir}/$1" ]; then
    echo "missing file: $1" >&2
    failures=$((failures + 1))
  fi
}

require_dir() {
  if [ ! -d "${release_dir}/$1" ]; then
    echo "missing directory: $1" >&2
    failures=$((failures + 1))
  fi
}

# --- the manifest ------------------------------------------------------------
require_file "${platform_dir}/zoal-atc.xpl"
require_file "${platform_dir}/${skyscript_library}"
# The panel is a real page, not a placeholder directory. Since phase 24 the
# bundler emits exactly these two files from one entry point, so the manifest is
# the whole panel rather than most of it: the old layout also shipped bridge.js,
# which was never listed here, and a release missing it passed this check and
# produced a window that threw on load.
require_file "apps/zoal-atc/index.html"
require_file "apps/zoal-atc/main.js"
require_file "apps/zoal-atc/main.css"
# Skyscript loads these at runtime; without them the window renders without its
# own chrome and the failure looks like a broken page rather than a packaging
# mistake.
require_dir "assets/icons"
require_file "assets/icons/x-circle.svg"
# The notification sound. Skyscript reads it from the assets directory at the
# moment a toast is raised, so a release without it fails silently: the toast
# appears and simply makes no sound, which is indistinguishable from a pilot
# having turned the sound off.
require_file "assets/notify.pcm"
require_file "licenses/skyscript/LICENSE"

# --- things that must NOT be there -------------------------------------------
if [ -e "${release_dir}/${platform_dir}/cef" ] ||
   [ -e "${release_dir}/${platform_dir}/Chromium Embedded Framework.framework" ] ||
   [ -e "${release_dir}/${platform_dir}/libcef.so" ] ||
   [ -e "${release_dir}/${platform_dir}/libcef.dll" ]; then
  echo "release ships a CEF runtime; Skyscript loads X-Plane's own since v0.5.0" >&2
  failures=$((failures + 1))
fi

if [ "${failures}" -ne 0 ]; then
  echo "release tree incomplete: ${failures} problem(s) under ${release_dir}" >&2
  exit 1
fi

echo "release tree OK under ${release_dir}"
