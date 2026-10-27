#!/usr/bin/env bash
set -euo pipefail

# Provision the two models every plugin ships with: whisper's, which hears the
# pilot, and Kokoro's voices, which are the controllers. Both go out inside the
# plugin folder, because a plugin without them can neither be heard by the
# console, which no longer transcribes, nor say anything to the pilot, since
# nothing else in the cockpit speaks. Pinned by checksum here and downloaded
# once into .cache/models.

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
root="${ZOAL_ATC_MODELS:-${repo_root}/.cache/models}"

stt_name=ggml-small.en-q5_1.bin
stt_sum=bfdff4894dcb76bbf647d56263ea2a96645423f1669176f4844a1bf8e478ad30
stt_url="https://huggingface.co/ggerganov/whisper.cpp/resolve/main/${stt_name}"

tts_name=kokoro-multi-lang-v1_0
# The voices are pinned by what the plugin ships, not by the archive they come
# in. sherpa-onnx re-uploaded this archive on 2026-10-26 with every English
# file byte for byte the same and a different archive checksum, and a release
# pinned to the archive failed on all three platforms. This is the checksum of
# the extracted English tree (see tree_sum), so a repack passes and a changed
# model, voice table or lexicon still fails.
tts_sum=1c023a2f8cd6119f669889f33d2fad49bbd501fd99bdf4effe7d7e55fbcc35db
tts_url="https://github.com/k2-fsa/sherpa-onnx/releases/download/tts-models/${tts_name}.tar.bz2"

sum() {
  local out
  out="$(shasum -a 256 "$1" 2>/dev/null || sha256sum "$1")"
  echo "${out%% *}"
}

# tree_sum is one checksum for every file under a directory: each file's
# checksum and its path, in a fixed order, checksummed together. The marker
# this script writes is left out.
tree_sum() {
  local listing
  listing="$(mktemp)"
  (
    cd "$1"
    find . -type f ! -name .content-sha256 | LC_ALL=C sort | while IFS= read -r file; do
      printf '%s  %s\n' "$(sum "${file}")" "${file}"
    done
  ) > "${listing}"
  sum "${listing}"
  rm -f "${listing}"
}

mkdir -p "${root}"

if [ ! -f "${root}/${stt_name}" ] || [ "$(sum "${root}/${stt_name}")" != "${stt_sum}" ]; then
  echo "Downloading ${stt_name}..."
  curl -fsSL -o "${root}/${stt_name}.partial" "${stt_url}"
  actual="$(sum "${root}/${stt_name}.partial")"
  if [ "${actual}" != "${stt_sum}" ]; then
    rm -f "${root}/${stt_name}.partial"
    echo "${stt_name} checksum is ${actual}, want ${stt_sum}" >&2
    exit 1
  fi
  mv "${root}/${stt_name}.partial" "${root}/${stt_name}"
fi

# The voices are a directory; a marker holding the shipped tree's checksum says
# the extraction finished and is the pinned content, not merely that it began.
if [ "$(cat "${root}/${tts_name}/.content-sha256" 2>/dev/null || true)" != "${tts_sum}" ]; then
  echo "Downloading ${tts_name}..."
  work="$(mktemp -d)"
  trap 'rm -rf "${work}"' EXIT
  curl -fsSL -o "${work}/${tts_name}.tar.bz2" "${tts_url}"
  tar -xjf "${work}/${tts_name}.tar.bz2" -C "${work}"
  # English only: the Chinese lexicon, its text-normalisation rules and its
  # word dictionary are a quarter of what is not the model, and nothing here
  # speaks Chinese.
  rm -rf "${work}/${tts_name}/dict" "${work}/${tts_name}/lexicon-zh.txt" "${work}/${tts_name}/"*-zh.fst
  actual="$(tree_sum "${work}/${tts_name}")"
  if [ "${actual}" != "${tts_sum}" ]; then
    echo "${tts_name} content checksum is ${actual}, want ${tts_sum}" >&2
    exit 1
  fi
  rm -rf "${root:?}/${tts_name}"
  mv "${work}/${tts_name}" "${root}/${tts_name}"
  echo "${tts_sum}" > "${root}/${tts_name}/.content-sha256"
fi
