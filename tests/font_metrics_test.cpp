#include "zoal_atc/gui/font_metrics.hpp"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {

int g_failures = 0;

void expect_true(bool condition, const char *label) {
  if (condition) {
    return;
  }
  std::cerr << label << ": expected true\n";
  ++g_failures;
}

void put16(std::string &out, std::size_t at, int value) {
  out[at] = static_cast<char>((value >> 8) & 0xFF);
  out[at + 1] = static_cast<char>(value & 0xFF);
}

void put32(std::string &out, std::size_t at, std::uint32_t value) {
  for (int i = 0; i < 4; ++i) {
    out[at + static_cast<std::size_t>(i)] =
        static_cast<char>((value >> (24 - 8 * i)) & 0xFF);
  }
}

// A font file with only what font_line_to_em reads: the offset table, a head
// table (unitsPerEm) and an hhea table (ascender, descender).
std::string font(int units, int ascender, int descender) {
  const std::size_t head = 12 + 2 * 16;
  const std::size_t hhea = head + 54;
  std::string out(hhea + 36, '\0');
  put32(out, 0, 0x00010000);
  put16(out, 4, 2);
  out.replace(12, 4, "head");
  put32(out, 12 + 8, static_cast<std::uint32_t>(head));
  out.replace(28, 4, "hhea");
  put32(out, 28 + 8, static_cast<std::uint32_t>(hhea));
  put16(out, head + 18, units);
  put16(out, hhea + 4, ascender);
  put16(out, hhea + 6, descender & 0xFFFF);
  return out;
}

bool near(std::optional<float> got, float want) {
  return got.has_value() && std::fabs(*got - want) < 1e-5f;
}

void reads_the_line_of_real_typefaces() {
  // Inter 4.1 and Roboto, as X-Plane ships it.
  expect_true(near(zoal_atc::gui::font_line_to_em(font(2048, 1984, -494)),
                   1.2099609375f),
              "Inter");
  expect_true(near(zoal_atc::gui::font_line_to_em(font(2048, 1900, -500)),
                   1.171875f),
              "Roboto");
}

void refuses_what_is_not_a_font() {
  expect_true(!zoal_atc::gui::font_line_to_em(std::string()).has_value(),
              "empty");
  expect_true(!zoal_atc::gui::font_line_to_em(std::string("not a font")).has_value(),
              "text");
  std::string cut = font(2048, 1984, -494);
  cut.resize(60);
  expect_true(!zoal_atc::gui::font_line_to_em(cut).has_value(),
              "tables past the end");
  expect_true(!zoal_atc::gui::font_line_to_em(font(0, 1984, -494)).has_value(),
              "no units");
  expect_true(!zoal_atc::gui::font_line_to_em(font(2048, 100, -50)).has_value(),
              "a line too short to be real");
}

} // namespace

int main() {
  reads_the_line_of_real_typefaces();
  refuses_what_is_not_a_font();
  if (g_failures != 0) {
    std::cerr << g_failures << " failure(s)\n";
    return EXIT_FAILURE;
  }
  std::cout << "font metrics: all checks passed\n";
  return EXIT_SUCCESS;
}
