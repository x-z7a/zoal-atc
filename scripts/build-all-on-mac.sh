#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
dist_dir="${DIST_DIR:-${repo_root}/dist}"
stage_dir="${dist_dir}/.release-all-stage"
release_dir="${dist_dir}/zoal-atc"
archive_path="${dist_dir}/zoal-atc.zip"
linux_builder_image="${ZOAL_ATC_LINUX_BUILDER_IMAGE:-zoal-atc-linux-release:local}"
skyscript_root="${ZOAL_ATC_SKYSCRIPT_ROOT:-${repo_root}/.cache/skyscript/SkyScript-lib}"

cleanup() {
  cmake -E rm -rf "${stage_dir}"
}

trap cleanup EXIT

if [ "$(uname -s)" != "Darwin" ]; then
  echo "build-all-on-mac.sh must be run on macOS" >&2
  exit 1
fi

required_commands=(cmake docker make npm)
for command_name in "${required_commands[@]}"; do
  if ! command -v "${command_name}" >/dev/null 2>&1; then
    echo "Missing required command: ${command_name}" >&2
    exit 1
  fi
done

if ! docker info >/dev/null 2>&1; then
  echo "Docker is installed but its daemon is not running. Start Docker Desktop and retry." >&2
  exit 1
fi

case "$(uname -m)" in
  arm64|aarch64) mac_arch=arm64 ;;
  x86_64|amd64) mac_arch=x64 ;;
  *)
    echo "Unsupported macOS architecture: $(uname -m)" >&2
    exit 1
    ;;
esac

cmake -E rm -rf "${stage_dir}"
cmake -E make_directory "${dist_dir}" "${stage_dir}"

cmake -E rm -rf \
  "${release_dir}" \
  "${archive_path}" \
  "${dist_dir}"/zoal-atc-macos-* \
  "${dist_dir}"/zoal-atc-windows-* \
  "${dist_dir}"/zoal-atc-linux-*
cmake -E make_directory "${release_dir}"

echo "==> Building macOS ${mac_arch} release"
make -C "${repo_root}" plugin gui-build \
  ZOAL_ATC_SKYSCRIPT_ROOT="${skyscript_root}"
ZOAL_ATC_SKYSCRIPT_ROOT="${skyscript_root}" \
"${repo_root}/scripts/package-release.sh" \
  mac_x64 \
  "${repo_root}/build-plugin/zoal_atc.xpl" \
  "${release_dir}"
ZOAL_ATC_SKYSCRIPT_ROOT="${skyscript_root}" \
  "${repo_root}/scripts/verify-release-tree.sh" "${release_dir}" mac_x64

echo "==> Building Windows x64 release"
windows_stage="${stage_dir}/windows-x64"
"${repo_root}/scripts/build-windows-on-mac.sh" "${windows_stage}"
ZOAL_ATC_SKYSCRIPT_ROOT="${skyscript_root}" \
"${repo_root}/scripts/package-release.sh" \
  win_x64 \
  "${windows_stage}/win_x64/zoal-atc.xpl" \
  "${release_dir}"
"${repo_root}/scripts/verify-release-tree.sh" "${release_dir}" win_x64

echo "==> Building Linux x64 release in Docker"
docker build \
  --progress plain \
  --platform linux/amd64 \
  --file "${repo_root}/scripts/docker/linux-release.Dockerfile" \
  --tag "${linux_builder_image}" \
  "${repo_root}/scripts/docker"
docker run \
  --rm \
  --platform linux/amd64 \
  --volume "${repo_root}:/src:ro" \
  --volume "${dist_dir}:/out" \
  "${linux_builder_image}" \
  /src/scripts/build-linux-release-in-docker.sh /out
"${repo_root}/scripts/verify-release-tree.sh" "${release_dir}" lin_x64

find "${release_dir}" -name .DS_Store -delete
(cd "${dist_dir}" && cmake -E tar cf "$(basename "${archive_path}")" --format=zip "zoal-atc")

echo
echo "All release builds merged into:"
echo "  ${release_dir}"
echo "  ${archive_path}"
