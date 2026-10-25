#!/usr/bin/env bash
set -euo pipefail

# The one assembler for every zoal-atc release tree.
#
# The plugin, the panel app, the Skyscript runtime and the two models always go
# in. The models are whisper's, which hears the pilot now that the console no
# longer transcribes, and Kokoro's voices, which are the controllers now that
# nothing else in the cockpit speaks; together about 560MB, and a plugin
# without either cannot be used. On Windows and Linux the speech runtime's
# shared libraries sit beside the .xpl, which is where the build leaves them.
#
# It never wipes the destination, because build-all-on-mac.sh merges three
# platforms into a single tree. Callers that want a clean tree remove it first.
# It does not verify either - scripts/verify-release-tree.sh is a separate step
# so a merged tree is checked once per platform rather than once per copy.

if [ "$#" -ne 3 ]; then
  echo "usage: package-release.sh <plugin-dir> <plugin> <release-dir>" >&2
  exit 2
fi

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
plugin_dir="$1"
plugin_source="$2"
release_dir="$3"

if [ ! -f "${plugin_source}" ]; then
  echo "Plugin artifact not found: ${plugin_source}" >&2
  exit 1
fi

cmake -E make_directory \
  "${release_dir}/${plugin_dir}" \
  "${release_dir}/apps/zoal-atc"

cmake -E copy "${plugin_source}" "${release_dir}/${plugin_dir}/zoal-atc.xpl"
plugin_build_dir="$(dirname "${plugin_source}")"
for runtime in sherpa-onnx-c-api.dll onnxruntime.dll onnxruntime_providers_shared.dll \
               libsherpa-onnx-c-api.so libonnxruntime.so; do
  if [ -f "${plugin_build_dir}/${runtime}" ]; then
    cmake -E copy "${plugin_build_dir}/${runtime}" "${release_dir}/${plugin_dir}/${runtime}"
  fi
done

models_dir="${ZOAL_ATC_MODELS:-${repo_root}/.cache/models}"
"${repo_root}/scripts/ensure-models.sh"
cmake -E make_directory "${release_dir}/models"
cmake -E copy "${models_dir}/ggml-small.en-q5_1.bin" "${release_dir}/models/ggml-small.en-q5_1.bin"
cmake -E copy_directory "${models_dir}/kokoro-multi-lang-v1_0" "${release_dir}/models/kokoro-multi-lang-v1_0"
cmake -E make_directory "${release_dir}/licenses"
cmake -E copy "${repo_root}/licenses-third-party/THIRD-PARTY.txt" "${release_dir}/licenses/THIRD-PARTY.txt"
cmake -E copy_directory "${repo_root}/gui/zoal-atc/dist" "${release_dir}/apps/zoal-atc"
# Skyscript is not optional: a release without it ships a plugin with no GUI.
"${repo_root}/scripts/package-skyscript-runtime.sh" \
  "${ZOAL_ATC_SKYSCRIPT_ROOT:-${repo_root}/.cache/skyscript/SkyScript-lib}" \
  "${plugin_dir}" \
  "${release_dir}"

printf '%s\n' \
  'zoal-atc plugin release' \
  '' \
  'This archive contains the X-Plane plugin binary, the in-sim panel, and the' \
  'models it hears and speaks with: whisper for push-to-talk, and Kokoro for' \
  'the controllers, each position in a voice of its own on the COM radio.' \
  '' \
  '1. Drop this zoal-atc folder into X-Plane Resources/plugins.' \
  '2. Open the panel with Plugins > zoal-atc > Toggle In-Sim Panel.' \
  '3. Bind the X-Plane command zoal_atc/ptt to your push-to-talk input.' \
  '' \
  'The plugin talks to the hosted zoal-atc console at ws://atc.zoal.app/plugin.' \
  'That is the default and needs no setup. To use a console you run yourself,' \
  'set url in <X-Plane>/Output/preferences/zoal_atc.cfg, which the plugin' \
  'writes on first run.' \
  > "${release_dir}/README.txt"
