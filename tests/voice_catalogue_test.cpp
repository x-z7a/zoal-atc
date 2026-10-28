#include "zoal_atc/tts/voice_catalogue.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

using namespace zoal_atc::tts;

namespace {

void require(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

std::string slurp(const std::filesystem::path &path) {
  std::ifstream in(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void write(const std::filesystem::path &path, const std::string &content) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out.write(content.data(), static_cast<std::streamsize>(content.size()));
}

// The numbers the console has always sent mean the voices they always meant:
// the stock speakers first, in the model's order.
void test_stock_speakers_keep_their_numbers() {
  const auto &catalogue = voice_catalogue();
  require(catalogue.size() > static_cast<std::size_t>(kStockSpeakers), "the catalogue adds voices");
  require(std::string(catalogue[16].name) == "am_michael", "voice 17 is am_michael");
  require(std::string(catalogue[27].name) == "bm_lewis", "voice 28 is bm_lewis");
  for (int i = 0; i < kStockSpeakers; ++i) {
    const auto &voice = catalogue[static_cast<std::size_t>(i)];
    require(voice.parts[0].speaker == i && voice.parts[1].speaker == -1, "a stock voice is itself");
  }
  require(catalogue[20].lexicon == Lexicon::British && catalogue[16].lexicon == Lexicon::American,
          "British voices speak British English");
  for (const auto &voice : catalogue) {
    require(voice.speed >= 1.0f && voice.speed <= 1.6f, "every speed is a controller's");
  }
}

// The same bytes scripts/voice-catalogue.py appends.
void test_speaker_count_suffix() {
  const std::string suffix = speaker_count_suffix(80);
  const std::string expected = "\x72\x10\x0a\x0a" "n_speakers" "\x12\x02" "80";
  require(suffix == expected, "the speaker count is one ModelProto metadata entry");
}

// A blend is the weighted sum of its speakers' tables.
void test_blends_are_weighted_sums() {
  std::vector<float> stock(static_cast<std::size_t>(kStockSpeakers) * kStyleFloats);
  for (int s = 0; s < kStockSpeakers; ++s) {
    std::fill_n(stock.begin() + static_cast<std::ptrdiff_t>(static_cast<std::size_t>(s) * kStyleFloats),
                kStyleFloats, static_cast<float>(s));
  }
  const auto table = blend_catalogue(stock);
  const auto &catalogue = voice_catalogue();
  require(table.size() == catalogue.size() * kStyleFloats, "one table per voice");
  require(table[16 * kStyleFloats] == 16.0f, "a stock voice is copied");
  const auto &first = catalogue[static_cast<std::size_t>(kStockSpeakers)];
  const float want = first.parts[0].weight * static_cast<float>(first.parts[0].speaker) +
                     first.parts[1].weight * static_cast<float>(first.parts[1].speaker);
  const float got = table[static_cast<std::size_t>(kStockSpeakers) * kStyleFloats + 7];
  require(got > want - 1e-4f && got < want + 1e-4f, "the first blend is its weighted sum");
  require(blend_catalogue(std::vector<float>(10)).empty(), "a table that is not the stock one is refused");
}

// Prepared once, the model ends with its speaker count and the table sits
// beside it; prepared again, nothing grows.
void test_prepare_is_idempotent() {
  const auto dir = std::filesystem::temp_directory_path() / "zoal_atc_voice_catalogue_test";
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir);
  write(dir / "model.onnx", "a model");
  write(dir / "voices.bin",
        std::string(static_cast<std::size_t>(kStockSpeakers) * kStyleFloats * sizeof(float), '\0'));

  std::string error;
  require(prepare_catalogue(dir.string(), &error), "prepared: " + error);
  const std::string suffix = speaker_count_suffix(static_cast<int>(voice_catalogue().size()));
  require(slurp(dir / "model.onnx") == "a model" + suffix, "the model is given its speaker count");
  require(std::filesystem::file_size(dir / kCatalogueVoicesFile) ==
              voice_catalogue().size() * kStyleFloats * sizeof(float),
          "the catalogue's table is written");
  require(slurp(dir / "lexicon-aviation-us.txt").find("niner") != std::string::npos,
          "niner is in the aviation lexicon");

  require(prepare_catalogue(dir.string(), &error), "prepared again: " + error);
  require(slurp(dir / "model.onnx") == "a model" + suffix, "the suffix is not appended twice");
  std::filesystem::remove_all(dir);
}

// A folder without the stock table is left alone: the model keeps the speaker
// count its own table matches.
void test_prepare_refuses_a_foreign_table() {
  const auto dir = std::filesystem::temp_directory_path() / "zoal_atc_voice_catalogue_foreign";
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir);
  write(dir / "model.onnx", "a model");
  write(dir / "voices.bin", "short");
  std::string error;
  require(!prepare_catalogue(dir.string(), &error), "refused");
  require(slurp(dir / "model.onnx") == "a model", "the model is untouched");
  std::filesystem::remove_all(dir);
}

}  // namespace

int main() {
  test_stock_speakers_keep_their_numbers();
  test_speaker_count_suffix();
  test_blends_are_weighted_sums();
  test_prepare_is_idempotent();
  test_prepare_refuses_a_foreign_table();
  std::cout << "voice catalogue tests passed\n";
  return 0;
}
