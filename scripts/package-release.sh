#!/usr/bin/env bash
set -euo pipefail

# The one assembler for every zoal-atc release tree.
#
# The plugin, the panel app and the Skyscript runtime always go in, and that is
# now the whole release: the console is hosted rather than shipped, so the
# release stopped carrying a console binary and the ~180MB Whisper STT model the
# console alone ever read. Both shapes used to be assembled a second time inline
# in the Makefile, which is how they drifted.
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
cmake -E copy_directory "${repo_root}/gui/zoal-atc/dist" "${release_dir}/apps/zoal-atc"
# Skyscript is not optional: a release without it ships a plugin with no GUI.
"${repo_root}/scripts/package-skyscript-runtime.sh" \
  "${ZOAL_ATC_SKYSCRIPT_ROOT:-${repo_root}/.cache/skyscript/SkyScript-lib}" \
  "${plugin_dir}" \
  "${release_dir}"

printf '%s\n' \
  'zoal-atc plugin release' \
  '' \
  'This archive contains the X-Plane plugin binary and the in-sim panel.' \
  '' \
  '1. Drop this zoal-atc folder into X-Plane Resources/plugins.' \
  '2. Open the panel with Plugins > zoal-atc > Toggle In-Sim Panel.' \
  '3. Bind the X-Plane command zoal_atc/ptt to your push-to-talk input.' \
  '' \
  'The plugin talks to a zoal-atc console over WebSocket. Point it at one with' \
  '<X-Plane>/Output/preferences/zoal_atc.cfg; it defaults to 127.0.0.1:8765.' \
  > "${release_dir}/README.txt"
