#!/usr/bin/env bash
set -euo pipefail

# Package the Skyscript runtime beside the plugin.
#
# That is the shared library and the assets, and nothing else. Since Skyscript
# v0.5.0 the library resolves the CEF runtime against X-Plane's own install
# (on macOS: @executable_path/../Frameworks/Chromium Embedded Framework.framework),
# so a release ships no CEF tree. Copying one would add ~200MB that X-Plane
# already has, and the copy would not be the one actually loaded.

if [ "$#" -ne 3 ]; then
  echo "usage: package-skyscript-runtime.sh <skyscript-root> <plugin-platform-dir> <release-dir>" >&2
  exit 2
fi

skyscript_root="$1"
plugin_dir="$2"
release_dir="$3"
platform_dir="${release_dir}/${plugin_dir}"
skyscript_platform="${plugin_dir}"

case "${skyscript_platform}" in
  mac_x64) library_name="libSkyScriptLib.dylib" ;;
  lin_x64) library_name="libSkyScriptLib.so" ;;
  win_x64) library_name="SkyScriptLib.dll" ;;
  *)
    echo "Unsupported Skyscript platform directory: ${plugin_dir}" >&2
    exit 1
    ;;
esac

library_source="${skyscript_root}/lib/${skyscript_platform}/${library_name}"
assets_source="${skyscript_root}/assets"

if [ ! -f "${library_source}" ]; then
  echo "Skyscript library not found: ${library_source}" >&2
  exit 1
fi
if [ ! -d "${assets_source}" ]; then
  echo "Skyscript assets not found: ${assets_source}" >&2
  exit 1
fi

cmake -E make_directory "${platform_dir}" "${release_dir}/assets" "${release_dir}/licenses/skyscript"
cmake -E copy "${library_source}" "${platform_dir}/${library_name}"
cmake -E copy_directory "${assets_source}" "${release_dir}/assets"

if [ -f "${skyscript_root}/LICENSE" ]; then
  cmake -E copy "${skyscript_root}/LICENSE" "${release_dir}/licenses/skyscript/LICENSE"
fi

# Record which Skyscript a release was built against, so a bug report can name
# it without anyone having to guess from a file date.
if [ -f "${skyscript_root}/SKYSCRIPT_VERSION" ]; then
  cmake -E copy "${skyscript_root}/SKYSCRIPT_VERSION" "${release_dir}/licenses/skyscript/SKYSCRIPT_VERSION"
fi

echo "Packaged Skyscript runtime for ${plugin_dir} (CEF provided by X-Plane)"
