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
tts_sum=c5f7e2d2caf082bc1d20fb70334a61d99d20b484500aad32e7cf84c128ea3298
tts_url="https://github.com/k2-fsa/sherpa-onnx/releases/download/tts-models/${tts_name}.tar.bz2"

sum() {
  local out
  out="$(shasum -a 256 "$1" 2>/dev/null || sha256sum "$1")"
  echo "${out%% *}"
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

# The voices are a directory; a marker holding the archive's checksum says the
# extraction finished from the pinned archive, not merely that it began.
if [ "$(cat "${root}/${tts_name}/.archive-sha256" 2>/dev/null || true)" != "${tts_sum}" ]; then
  echo "Downloading ${tts_name}..."
  work="$(mktemp -d)"
  trap 'rm -rf "${work}"' EXIT
  curl -fsSL -o "${work}/${tts_name}.tar.bz2" "${tts_url}"
  actual="$(sum "${work}/${tts_name}.tar.bz2")"
  if [ "${actual}" != "${tts_sum}" ]; then
    echo "${tts_name} checksum is ${actual}, want ${tts_sum}" >&2
    exit 1
  fi
  tar -xjf "${work}/${tts_name}.tar.bz2" -C "${work}"
  # English only: the Chinese lexicon, its text-normalisation rules and its
  # word dictionary are a quarter of what is not the model, and nothing here
  # speaks Chinese.
  rm -rf "${work}/${tts_name}/dict" "${work}/${tts_name}/lexicon-zh.txt" "${work}/${tts_name}/"*-zh.fst
  rm -rf "${root:?}/${tts_name}"
  mv "${work}/${tts_name}" "${root}/${tts_name}"
  echo "${tts_sum}" > "${root}/${tts_name}/.archive-sha256"
fi
