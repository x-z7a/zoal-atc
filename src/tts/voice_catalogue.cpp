#include "zoal_atc/tts/voice_catalogue.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>

namespace zoal_atc::tts {

namespace {

#include "aviation_lexicon.inc"

const std::vector<CatalogueVoice> kCatalogue = {
#include "voice_catalogue_table.inc"
};

std::string varint(std::uint64_t n) {
  std::string out;
  do {
    auto byte = static_cast<unsigned char>(n & 0x7Fu);
    n >>= 7;
    if (n != 0) {
      byte = static_cast<unsigned char>(byte | 0x80u);
    }
    out.push_back(static_cast<char>(byte));
  } while (n != 0);
  return out;
}

std::string field(std::uint64_t number, const std::string &payload) {
  return varint((number << 3) | 2u) + varint(payload.size()) + payload;
}

bool read_file(const std::filesystem::path &path, std::string *out) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return false;
  }
  out->assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  return true;
}

// write_if_different writes a whole file through a temporary, so a reader
// never sees half of one, and leaves an identical file untouched.
bool write_if_different(const std::filesystem::path &path, const std::string &content) {
  std::string existing;
  if (read_file(path, &existing) && existing == content) {
    return true;
  }
  const std::filesystem::path partial = path.string() + ".partial";
  {
    std::ofstream out(partial, std::ios::binary | std::ios::trunc);
    if (!out) {
      return false;
    }
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
    if (!out) {
      return false;
    }
  }
  std::error_code failed;
  std::filesystem::rename(partial, path, failed);
  return !failed;
}

// model_ends_with reports whether the model's last bytes are suffix.
bool model_ends_with(const std::filesystem::path &model, const std::string &suffix) {
  std::ifstream in(model, std::ios::binary | std::ios::ate);
  if (!in) {
    return false;
  }
  const auto size = static_cast<std::streamoff>(in.tellg());
  const auto length = static_cast<std::streamoff>(suffix.size());
  if (size < length) {
    return false;
  }
  in.seekg(size - length);
  std::string tail(suffix.size(), '\0');
  in.read(tail.data(), length);
  return in && tail == suffix;
}

}  // namespace

const std::vector<CatalogueVoice> &voice_catalogue() { return kCatalogue; }

std::vector<float> blend_catalogue(const std::vector<float> &stock) {
  const std::size_t speakers = static_cast<std::size_t>(kStockSpeakers);
  if (stock.size() != speakers * kStyleFloats) {
    return {};
  }
  std::vector<float> out(kCatalogue.size() * kStyleFloats, 0.0f);
  for (std::size_t v = 0; v < kCatalogue.size(); ++v) {
    float *into = out.data() + v * kStyleFloats;
    for (const VoicePart &part : kCatalogue[v].parts) {
      if (part.speaker < 0 || part.speaker >= kStockSpeakers) {
        continue;
      }
      const float *from = stock.data() + static_cast<std::size_t>(part.speaker) * kStyleFloats;
      for (std::size_t i = 0; i < kStyleFloats; ++i) {
        into[i] += part.weight * from[i];
      }
    }
  }
  return out;
}

std::string speaker_count_suffix(int count) {
  return field(14, field(1, "n_speakers") + field(2, std::to_string(count)));
}

const char *aviation_lexicon(Lexicon lexicon) {
  return lexicon == Lexicon::British ? kAviationLexiconGB : kAviationLexiconUS;
}

const char *aviation_lexicon_file(Lexicon lexicon) {
  return lexicon == Lexicon::British ? "lexicon-aviation-gb.txt" : "lexicon-aviation-us.txt";
}

bool prepare_catalogue(const std::string &model_dir, std::string *error) {
  const std::filesystem::path dir(model_dir);
  const auto fail = [error](const std::string &why) {
    if (error != nullptr) {
      *error = why;
    }
    return false;
  };

  for (const Lexicon lexicon : {Lexicon::American, Lexicon::British}) {
    if (!write_if_different(dir / aviation_lexicon_file(lexicon), aviation_lexicon(lexicon))) {
      return fail(std::string("cannot write ") + aviation_lexicon_file(lexicon));
    }
  }

  std::string stock_bytes;
  if (!read_file(dir / "voices.bin", &stock_bytes) ||
      stock_bytes.size() != static_cast<std::size_t>(kStockSpeakers) * kStyleFloats * sizeof(float)) {
    return fail("voices.bin is not Kokoro v1.0's stock table");
  }
  std::vector<float> stock(stock_bytes.size() / sizeof(float));
  std::copy(stock_bytes.begin(), stock_bytes.end(), reinterpret_cast<char *>(stock.data()));
  const std::vector<float> table = blend_catalogue(stock);
  const std::string table_bytes(reinterpret_cast<const char *>(table.data()), table.size() * sizeof(float));
  if (!write_if_different(dir / kCatalogueVoicesFile, table_bytes)) {
    return fail(std::string("cannot write ") + kCatalogueVoicesFile);
  }

  // Last, because a model told it has more speakers than the stock table holds
  // no longer loads with that table.
  const std::string suffix = speaker_count_suffix(static_cast<int>(kCatalogue.size()));
  const std::filesystem::path model = dir / "model.onnx";
  if (!model_ends_with(model, suffix)) {
    std::ofstream out(model, std::ios::binary | std::ios::app);
    out.write(suffix.data(), static_cast<std::streamsize>(suffix.size()));
    if (!out) {
      return fail("cannot give model.onnx its speaker count");
    }
  }
  return true;
}

}  // namespace zoal_atc::tts
