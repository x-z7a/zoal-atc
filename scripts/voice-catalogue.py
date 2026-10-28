#!/usr/bin/env python3
"""The controllers' voice catalogue, and the speed each voice speaks at.

The console names a voice by number; voice n is catalogue entry n-1. Entries
0-53 are Kokoro v1.0's own speakers in the model's order, so every number the
console has ever sent still means the same voice. Entries 54 onward are blends:
a weighted average of two speakers' style vectors, which Kokoro speaks as a new
voice. They exist because six of the seven accents Kokoro has besides American
and British come with one to five voices, and a field needs more people than
that.

Every voice gets its own speed, because Kokoro's speakers talk at their own
pace — measured here between 2.2 and 3.0 words a second — and a controller
does not. Each is timed saying ordinary transmissions at speed 1.0 and given the
factor that brings it to TARGET_WPS.

Run with sherpa-onnx installed (pip install sherpa-onnx numpy) against the
Kokoro model directory; it writes src/tts/voice_catalogue_table.inc, which is
committed so a build needs none of this, and src/tts/aviation_lexicon.inc.

    python3 scripts/voice-catalogue.py .cache/models/kokoro-multi-lang-v1_0
"""
import os
import sys
import tempfile

# The stock speakers, in the order Kokoro v1.0's voices.bin and model metadata
# carry them.
STOCK = ("af_alloy af_aoede af_bella af_heart af_jessica af_kore af_nicole af_nova af_river "
         "af_sarah af_sky am_adam am_echo am_eric am_fenrir am_liam am_michael am_onyx am_puck "
         "am_santa bf_alice bf_emma bf_isabella bf_lily bm_daniel bm_fable bm_george bm_lewis "
         "ef_dora em_alex ff_siwis hf_alpha hf_beta hm_omega hm_psi if_sara im_nicola jf_alpha "
         "jf_gongitsune jf_nezumi jf_tebukuro jm_kumo pf_dora pm_alex pm_santa zf_xiaobei "
         "zf_xiaoni zf_xiaoxiao zf_xiaoyi zm_yunjian zm_yunxi zm_yunxia zm_yunyang em_santa").split()

# The blends, appended in this order. Changing one renumbers nothing before it;
# never reorder or remove an entry, only append.
BLENDS = [
    [("ff_siwis", .7), ("bf_isabella", .3)],
    [("ff_siwis", .6), ("bm_george", .4)],
    [("ff_siwis", .6), ("am_eric", .4)],
    [("em_alex", .5), ("em_santa", .5)],
    [("ef_dora", .7), ("bf_emma", .3)],
    [("em_alex", .7), ("am_adam", .3)],
    [("hm_omega", .5), ("hm_psi", .5)],
    [("hf_alpha", .5), ("hf_beta", .5)],
    [("hm_psi", .7), ("bm_daniel", .3)],
    [("im_nicola", .7), ("am_michael", .3)],
    [("if_sara", .7), ("af_sarah", .3)],
    [("im_nicola", .6), ("if_sara", .4)],
    [("jm_kumo", .7), ("am_adam", .3)],
    [("jf_alpha", .5), ("jf_nezumi", .5)],
    [("pm_alex", .5), ("pm_santa", .5)],
    [("pf_dora", .7), ("af_nova", .3)],
    [("zm_yunjian", .5), ("zm_yunyang", .5)],
    [("zf_xiaoyi", .5), ("zf_xiaoxiao", .5)],
]

# A controller's pace: about 205 words a minute.
TARGET_WPS = 3.4
SPEED_MIN, SPEED_MAX = 1.0, 1.6

CALIBRATION = [
    "Air Canada two one, turn left heading two niner zero, descend and maintain four thousand, "
    "vectors ILS runway two seven right approach.",
    "Delta one forty one, runway two seven right, wind two seven zero at one two, cleared to land.",
    "United four seventeen, contact Chicago Center one one eight point one five, good day.",
]


# Words a controller says that Kokoro's lexicons do not have. A word missing
# from the lexicon is not guessed at; it is silently left out, and "turn left
# heading one niner five" was spoken "turn left heading one five". Phonemes are
# the ones the stock lexicons use for the letters and words they are built from.
AVIATION = {
    "us": {
        "niner": "n ˈ I n ɚ",
        "ils": "ˈ I ˌ ɛ l ˈ ɛ s",
        "rnav": "ˈ ɑ ɹ n ˌ æ v",
        "rnp": "ˈ ɑ ɹ ˌ ɛ n p ˈ i",
        "vor": "v ˈ i ˌ O ˈ ɑ ɹ",
        "gps": "ʤ ˈ i p ˌ i ˈ ɛ s",
        "dme": "d ˈ i ˌ ɛ m ˈ i",
        "ndb": "ˈ ɛ n d ˌ i b ˈ i",
        "vfr": "v ˈ i ˌ ɛ f ˈ ɑ ɹ",
        "ifr": "ˈ I ˌ ɛ f ˈ ɑ ɹ",
        "atis": "ˈ A t ɪ s",
        "qnh": "k j ˈ u ˌ ɛ n ˈ A ʧ",
    },
}
AVIATION["gb"] = dict(AVIATION["us"], niner="n ˈ I n ə")


def lexicon_text(lex):
    return "".join(f"{word} {phonemes}\n" for word, phonemes in sorted(AVIATION[lex].items()))


def british(name):
    # British and Indian English are spoken with the British lexicon.
    return name[0] in "bh"


def entries():
    out = [(n, [(n, 1.0)]) for n in STOCK]
    for parts in BLENDS:
        out.append((" + ".join(f"{int(w * 100)}% {n}" for n, w in parts), parts))
    return out


def speaker_suffix(count):
    # ModelProto field 14, metadata_props: {key: "n_speakers", value: count}.
    def varint(n):
        b = bytearray()
        while True:
            x = n & 0x7F
            n >>= 7
            b.append(x | (0x80 if n else 0))
            if not n:
                return bytes(b)

    def field(num, payload):
        return varint((num << 3) | 2) + varint(len(payload)) + payload

    return field(14, field(1, b"n_speakers") + field(2, str(count).encode()))


def main():
    import numpy as np
    import sherpa_onnx

    model_dir = sys.argv[1]
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    index = {n: i for i, n in enumerate(STOCK)}
    catalogue = entries()
    stock = np.fromfile(os.path.join(model_dir, "voices.bin"), dtype=np.float32).reshape(len(STOCK), 510, 1, 256)
    table = np.stack([sum(w * stock[index[n]] for n, w in parts) for _, parts in catalogue]).astype(np.float32)

    with tempfile.TemporaryDirectory() as work:
        table.tofile(os.path.join(work, "voices.bin"))
        with open(os.path.join(model_dir, "model.onnx"), "rb") as src, open(os.path.join(work, "model.onnx"), "wb") as dst:
            dst.write(src.read())
            dst.write(speaker_suffix(len(catalogue)))
        engines = {}
        for lex in ("us", "gb"):
            with open(os.path.join(work, f"aviation-{lex}.txt"), "w") as f:
                f.write(lexicon_text(lex))
        for lex, lang in (("us", "en-us"), ("gb", "en")):
            engines[lex] = sherpa_onnx.OfflineTts(sherpa_onnx.OfflineTtsConfig(
                model=sherpa_onnx.OfflineTtsModelConfig(kokoro=sherpa_onnx.OfflineTtsKokoroModelConfig(
                    model=os.path.join(work, "model.onnx"), voices=os.path.join(work, "voices.bin"),
                    tokens=os.path.join(model_dir, "tokens.txt"), data_dir=os.path.join(model_dir, "espeak-ng-data"),
                    lexicon=os.path.join(model_dir, f"lexicon-{lex}-en.txt") + "," + os.path.join(work, f"aviation-{lex}.txt"), lang=lang), num_threads=4),
                max_num_sentences=1))
        words = sum(len(t.replace(",", "").split()) for t in CALIBRATION)
        rows = []
        for i, (label, parts) in enumerate(catalogue):
            lex = "gb" if british(parts[0][0]) else "us"
            seconds = sum(len(engines[lex].generate(t, sid=i, speed=1.0).samples) / 24000 for t in CALIBRATION)
            speed = round(min(SPEED_MAX, max(SPEED_MIN, TARGET_WPS / (words / seconds))), 2)
            rows.append((label, lex, parts, speed))
            print(f"{i + 1:3d} {label:34s} {words / seconds:.2f} w/s -> {speed:.2f}", flush=True)

    with open(os.path.join(root, "src", "tts", "aviation_lexicon.inc"), "w") as out:
        out.write("// Generated by scripts/voice-catalogue.py; do not edit by hand.\n")
        for lex in ("us", "gb"):
            out.write(f'constexpr const char *kAviationLexicon{lex.upper()} = R"lexicon({lexicon_text(lex)})lexicon";\n')
    with open(os.path.join(root, "src", "tts", "voice_catalogue_table.inc"), "w") as out:
        out.write("// Generated by scripts/voice-catalogue.py; do not edit by hand.\n")
        out.write("// {name, lexicon, {{speaker, weight}, {speaker, weight}}, speed}\n")
        for label, lex, parts, speed in rows:
            a = parts[0]
            b = parts[1] if len(parts) > 1 else (None, 0.0)
            second = f"{{{index[b[0]]}, {b[1]:.2f}f}}" if b[0] else "{-1, 0.0f}"
            lexicon = "Lexicon::British" if lex == "gb" else "Lexicon::American"
            out.write(f'{{"{label}", {lexicon}, {{{{{index[a[0]]}, {a[1]:.2f}f}}, {second}}}, {speed:.2f}f}},\n')


if __name__ == "__main__":
    main()
