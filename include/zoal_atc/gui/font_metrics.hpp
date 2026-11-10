#ifndef ZOAL_ATC_GUI_FONT_METRICS_HPP
#define ZOAL_ATC_GUI_FONT_METRICS_HPP

#include <cstddef>
#include <optional>
#include <string>

namespace zoal_atc::gui {

// How many ems tall a font's line is: (hhea ascender - hhea descender) /
// head unitsPerEm, read from a TrueType/OpenType file's bytes.
//
// CSS sizes a font by its em; Dear ImGui -- with stb_truetype or FreeType --
// sizes it by that whole line. The HUD's sizes are the React HUD's CSS sizes,
// so it multiplies each by this ratio to draw the glyphs CSS drew. The ratio is
// the font's own (Inter's line is 1.21 em, Roboto's 1.17), which is why it is
// read rather than written down.
//
// nullopt for anything that is not a font with sane head and hhea tables.
std::optional<float> font_line_to_em(const unsigned char *data,
                                     std::size_t size);
std::optional<float> font_line_to_em(const std::string &bytes);

} // namespace zoal_atc::gui

#endif
