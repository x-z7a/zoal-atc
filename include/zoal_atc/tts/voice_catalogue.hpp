#ifndef ZOAL_ATC_TTS_VOICE_CATALOGUE_HPP
#define ZOAL_ATC_TTS_VOICE_CATALOGUE_HPP

#include <cstddef>
#include <string>
#include <vector>

namespace zoal_atc::tts {

// The controllers' voices.
//
// The console names a voice by number, and voice n is catalogue entry n-1.
// Entries 0-53 are Kokoro v1.0's own speakers in the model's order, so every
// number the console sent before this catalogue still names the same voice.
// Entries 54 onward are blends: a weighted average of two speakers' style
// vectors, which Kokoro speaks as a voice of its own. They are why a French or
// Italian field has more than one or two people in it.
//
// Kokoro's speaker count is written in the model's metadata, and sherpa-onnx
// refuses a voices table of any other size. So the catalogue is prepared on the
// pilot's machine the first time the plugin starts: the table is written beside
// the stock one, and the model is given its new speaker count by appending one
// metadata entry, which a protobuf reader takes over the earlier one. Nothing
// is downloaded and nothing is hosted; the bundled model stays the one the
// release shipped, eighteen bytes longer.
//
// Each voice has its own speed, measured by scripts/voice-catalogue.py, because
// Kokoro's speakers talk at their own pace and a controller does not.

enum class Lexicon { American, British };

struct VoicePart {
  int speaker;   // a stock speaker, or -1 for none
  float weight;
};

struct CatalogueVoice {
  const char *name;
  Lexicon lexicon;
  VoicePart parts[2];
  float speed;
};

// kStockSpeakers is how many speakers Kokoro v1.0's voices.bin holds.
constexpr int kStockSpeakers = 54;
// kStyleFloats is one speaker's style table: 510 token lengths of 256.
constexpr std::size_t kStyleFloats = 510 * 256;

// kCatalogueVoicesFile is the prepared table, beside the stock voices.bin.
constexpr const char *kCatalogueVoicesFile = "voices-zoal.bin";

const std::vector<CatalogueVoice> &voice_catalogue();

// blend_catalogue is the catalogue's table from the stock one: kStockSpeakers
// speakers in, one style table per catalogue entry out. Empty when stock is
// not the size of the stock table.
std::vector<float> blend_catalogue(const std::vector<float> &stock);

// speaker_count_suffix is the bytes appended to the model to give it a speaker
// count: ONNX ModelProto field 14, one metadata entry, n_speakers = count.
std::string speaker_count_suffix(int count);

// aviation_lexicon is the words a controller says that Kokoro's lexicons lack.
const char *aviation_lexicon(Lexicon lexicon);
const char *aviation_lexicon_file(Lexicon lexicon);

// prepare_catalogue makes model_dir speak the catalogue: the aviation lexicons
// written, the catalogue's table written, and the model given its speaker
// count, in that order, so a failure part way leaves a model that still loads
// with the stock table. True when the catalogue is ready to use; on false,
// error says why and the stock voices are what the plugin has.
bool prepare_catalogue(const std::string &model_dir, std::string *error);

}  // namespace zoal_atc::tts

#endif  // ZOAL_ATC_TTS_VOICE_CATALOGUE_HPP
