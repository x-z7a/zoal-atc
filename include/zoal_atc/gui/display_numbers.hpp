#ifndef ZOAL_ATC_GUI_DISPLAY_NUMBERS_HPP
#define ZOAL_ATC_GUI_DISPLAY_NUMBERS_HPP

#include <string>
#include <string_view>

namespace zoal_atc::gui {

// display_numbers is a transcript line as it is shown: numbers spoken as words
// in English, Spanish or French are written as Arabic numerals. A run of two
// or more digit words is a number ("two seven zero" -> "270", "cuatro cero
// nueve seis" -> "4096"), a run with a point between is a decimal ("one two
// five point three five" -> "125.35"), and a run with thousand or hundred is
// its value ("three thousand five hundred" -> "3500"). A lone digit word is
// left as written ("Delta un", "one console") unless it follows a word that
// can only take a number ("runway two" -> "runway 2"). Punctuation ends a
// number and is kept; everything else is copied byte for byte. Only what is
// drawn changes: the line itself is still what is matched and logged.
std::string display_numbers(std::string_view text);

}  // namespace zoal_atc::gui

#endif  // ZOAL_ATC_GUI_DISPLAY_NUMBERS_HPP
