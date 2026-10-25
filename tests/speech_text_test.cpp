#include "zoal_atc/transport/speech_text.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

using zoal_atc::transport::radio_text;

namespace {

void require_eq(const std::string &got, const std::string &want,
                const std::string &message) {
  if (got != want) {
    std::cerr << "FAIL: " << message << "\n  got:  `" << got << "`\n  want: `"
              << want << "`\n";
    std::exit(1);
  }
}

}  // namespace

int main() {
  // Punctuation is kept: the neural voice pauses at a comma rather than
  // saying it, and the pause is where a controller breathes.
  require_eq(radio_text("November One Two, runway 07, taxi via Tango, hold short "
                        "of runway 07."),
             "November One Two, runway 07, taxi via Tango, hold short of runway 07.",
             "commas kept as pauses");

  // A comma with no trailing space still separates the words, and no leading
  // pause is emitted.
  require_eq(radio_text("contact tower;next: 118.800,ready"),
             "contact tower; next: one one eight point eight, ready",
             "every pause kept, words still separated");
  require_eq(radio_text(" , clear of runway"), "clear of runway",
             "no leading pause");
  require_eq(radio_text(""), "", "empty stays empty");

  // The live "one hundred thirty-three" report: a frequency is said digit by
  // digit with trailing decimal zeros dropped, not as a number.
  require_eq(radio_text("November One Two, departure frequency 133.400."),
             "November One Two, departure frequency one three three point four.",
             "frequency spoken digit-by-digit, trailing zeros dropped");
  require_eq(radio_text("contact tower on 118.325, so long."),
             "contact tower on one one eight point three two five, so long.",
             "mid-sentence frequency expanded");
  require_eq(radio_text("contact approach on 119.000."),
             "contact approach on one one niner point zero.",
             "niner cadence and all-zero decimal");

  // Other numbers are left to the voice's natural reading.
  require_eq(radio_text("climb and maintain 5000, expect 10000 in 10 minutes."),
             "climb and maintain 5000, expect 10000 in 10 minutes.",
             "plain numbers untouched");

  // Assigned headings use ATC digit cadence, not "two hundred forty".
  require_eq(radio_text("turn left heading 240."),
             "turn left heading two four zero.",
             "heading spoken digit-by-digit");
  require_eq(radio_text("Delta 141, turn left heading 270, contact departure 125.350."),
             "Delta 141, turn left heading two seven zero, contact departure one two five point three five.",
             "heading before a comma");
  require_eq(radio_text("fly heading 005, vector for spacing."),
             "fly heading zero zero five, vector for spacing.",
             "leading heading zeros spoken");
  require_eq(radio_text("runway 240, maintain 2400."),
             "runway 240, maintain 2400.",
             "non-heading integer groups remain natural");

  // A decimal that is not frequency-shaped is left alone.
  require_eq(radio_text("visibility 2.5, altimeter 29.92."),
             "visibility 2.5, altimeter 29.92.", "non-frequency decimals untouched");

  std::cout << "radio_text tests passed\n";
  return 0;
}
