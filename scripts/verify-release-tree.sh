#!/usr/bin/env bash
set -euo pipefail

# Verify an assembled release tree is complete.
#
# A release is the plugin, the runtime libraries it loads, the typeface its
# panel and HUD are set in, and the models. Miss any one and the failure
# surfaces inside X-Plane, which is the most expensive place to discover it and
# the hardest to read.
#
# There is no browser and no panel app: the panel and the HUD are drawn by the
# plugin itself with Dear ImGui, so a tree still carrying the old Skyscript
# runtime or apps/ folder was assembled from something stale.
#
# The models are present on every platform: the plugin hears the pilot with
# whisper and speaks the controllers with Kokoro, and nothing else does either.

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

speech_runtime=()
case "${platform_dir}" in
  mac_x64) ;;
  lin_x64)
    speech_runtime=(libsherpa-onnx-c-api.so libonnxruntime.so)
    ;;
  win_x64)
    speech_runtime=(sherpa-onnx-c-api.dll onnxruntime.dll onnxruntime_providers_shared.dll)
    ;;
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
for runtime in "${speech_runtime[@]+"${speech_runtime[@]}"}"; do
  require_file "${platform_dir}/${runtime}"
done
# What the plugin hears and speaks with. Without them it can do neither.
require_file "models/ggml-small.en-q5_1.bin"
require_file "models/kokoro-multi-lang-v1_0/model.onnx"
require_file "models/kokoro-multi-lang-v1_0/voices.bin"
require_file "models/kokoro-multi-lang-v1_0/tokens.txt"
require_dir "models/kokoro-multi-lang-v1_0/espeak-ng-data"
require_file "models/kokoro-multi-lang-v1_0/lexicon-us-en.txt"
require_file "models/kokoro-multi-lang-v1_0/lexicon-gb-en.txt"
require_file "licenses/THIRD-PARTY.txt"
# The panel's and the HUD's typeface. Without it they fall back to X-Plane's
# Roboto, which ImGui sets unevenly -- a release that looks broken without
# being broken.
require_file "fonts/Inter-Regular.ttf"
require_file "fonts/Inter-SemiBold.ttf"
require_file "fonts/Inter-Bold.ttf"
require_file "licenses/inter/LICENSE.txt"

# --- things that must NOT be there -------------------------------------------
for stale in "${platform_dir}/libSkyScriptLib.dylib" \
             "${platform_dir}/libSkyScriptLib.so" \
             "${platform_dir}/SkyScriptLib.dll" \
             "apps" "assets" "licenses/skyscript"; do
  if [ -e "${release_dir}/${stale}" ]; then
    echo "release ships ${stale}, left from the Skyscript panel" >&2
    failures=$((failures + 1))
  fi
done

if [ "${failures}" -ne 0 ]; then
  echo "release tree incomplete: ${failures} problem(s) under ${release_dir}" >&2
  exit 1
fi

echo "release tree OK under ${release_dir}"
