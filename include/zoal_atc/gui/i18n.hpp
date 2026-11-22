#ifndef ZOAL_ATC_GUI_I18N_HPP
#define ZOAL_ATC_GUI_I18N_HPP

#include <string>

namespace zoal_atc::gui {

// The panel and the HUD in the pilot's language.
//
// The pilot picks a reply language in Settings, and the plugin's own words
// follow it: every label, button and line of help the panel and the HUD draw
// is looked up here by its English, the way gettext does, so the English
// stays readable where it is used and a sentence nobody translated is shown
// in English rather than not at all. The words a pilot says — the call a
// button speaks, a transcript, a callsign — are never looked up: only what
// the plugin writes on the screen.
//
// The table is in i18n.cpp, one row per English string with its Spanish and
// French, and a test checks that every row has both.

// set_ui_language chooses the language the panel and the HUD are drawn in:
// "en", "es" or "fr"; anything else is English. Called from the main thread,
// which is the one that draws.
void set_ui_language(const std::string &language);

// ui_language is the language the panel and the HUD are drawn in.
const std::string &ui_language();

// tr is english in the current language, or english where it has no row. A
// label the console sent — "Request FL230", "Holding short 25" — is
// translated by its leading words where the table has them.
const char *tr(const char *english);
std::string tr(const std::string &english);

// translations is how many rows the table has, and translation_complete
// whether every row has Spanish and French: for the test.
std::size_t translations();
bool translation_complete(std::string *missing);

}  // namespace zoal_atc::gui

#endif  // ZOAL_ATC_GUI_I18N_HPP
