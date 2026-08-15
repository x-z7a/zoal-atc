#!/usr/bin/env bash
set -euo pipefail

output_dir="${1:-/out}"
work_dir=/work/zoal-atc

if [ "$(uname -s)" != "Linux" ] || [ "$(uname -m)" != "x86_64" ]; then
  echo "Linux release container must run as linux/amd64" >&2
  exit 1
fi

cmake -E rm -rf "${work_dir}"
cmake -E make_directory "${work_dir}"
# .cache is deliberately NOT excluded: it carries the pinned Skyscript bundle in
# from the host, which is what lets this container build without gh or a token.
rsync -a \
  --exclude .git \
  --exclude '/dist' \
  --exclude '/build*' \
  --exclude '/sdk' \
  --exclude '/vendor' \
  --exclude node_modules \
  /src/ "${work_dir}/"

make -C "${work_dir}" plugin gui-build
"${work_dir}/scripts/package-release.sh" \
  lin_x64 \
  "${work_dir}/build-plugin/zoal_atc.xpl" \
  "${output_dir}/zoal-atc"
