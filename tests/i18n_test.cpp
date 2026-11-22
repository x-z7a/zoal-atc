#include "zoal_atc/gui/i18n.hpp"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

namespace {

void require(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << "\n";
    std::exit(1);
  }
}

}  // namespace

int main() {
  using namespace zoal_atc::gui;
  std::string missing;
  require(translation_complete(&missing), "every row has Spanish and French: " + missing);
  require(translations() > 100, "the table holds the panel and the HUD");

  // English is the default, and the English comes back unchanged.
  set_ui_language("en");
  require(std::strcmp(tr("Settings"), "Settings") == 0, "English is English");

  set_ui_language("fr");
  require(ui_language() == "fr", "French is chosen");
  require(std::strcmp(tr("Settings"), "Réglages") == 0, "Settings is Réglages");
  // A console label is translated by its leading words, the rest kept.
  require(tr(std::string("Request FL230")) == "Demander FL230", "a prefix is translated");
  // Words nobody wrote down — a callsign, a transcript — pass through.
  require(tr(std::string("AC21")) == "AC21", "a callsign is not translated");

  set_ui_language("es");
  require(std::strcmp(tr("New flight"), "Nuevo vuelo") == 0, "Spanish");
  set_ui_language("de");
  require(ui_language() == "en", "German is English until it is written");
  require(std::strcmp(tr("New flight"), "New flight") == 0, "and says English");

  std::cout << "i18n tests passed\n";
  return 0;
}
