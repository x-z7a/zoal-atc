#include "zoal_atc/transport/speech_text.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

using zoal_atc::transport::speech_text;

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
  // The live "Quebec comma" report: commas must not survive into the synth.
  require_eq(
      speech_text("November One Two, runway 07, taxi via Tango, continue onto "
                  "Quebec, continue onto Alpha, continue onto Echo, hold short "
                  "of runway 07."),
      "November One Two runway 07 taxi via Tango continue onto Quebec continue "
      "onto Alpha continue onto Echo hold short of runway 07.",
      "commas voiced as word gaps");

  // Sentence-terminating periods are left for the synth's own prosody.
  require_eq(speech_text("November One Two, roger."), "November One Two roger.",
             "period preserved");

  // Semicolons and colons are vocalized too; a comma with no trailing space
  // still separates cleanly rather than running the words together.
  require_eq(speech_text("contact tower;next: 118.800,ready"),
             "contact tower next one one eight point eight ready",
             "other punctuation collapsed");

  // Idempotent on already-clean text, and no leading/trailing gap is emitted.
  require_eq(speech_text(" , clear of runway , "), "clear of runway",
             "leading/trailing punctuation trimmed");

  require_eq(speech_text(""), "", "empty stays empty");

  // The live "one hundred thirty-three" report: a frequency must be voiced
  // digit-by-digit with trailing decimal zeros dropped, not as a number.
  require_eq(speech_text("November One Two, departure frequency 133.400."),
             "November One Two departure frequency one three three point four.",
             "frequency spoken digit-by-digit, trailing zeros dropped");

  // Mid-sentence frequency (handoff form), decimal digits all significant.
  require_eq(speech_text("contact tower on 118.325, so long."),
             "contact tower on one one eight point three two five so long.",
             "mid-sentence frequency expanded");

  // Niner cadence and a .0 decimal keeping a single spoken zero.
  require_eq(speech_text("contact approach on 119.000."),
             "contact approach on one one niner point zero.",
             "niner cadence and all-zero decimal");

  // Non-frequency numbers are left for the synth's natural reading: altitudes
  // ("5000") and runway digits stay untouched.
  require_eq(speech_text("climb and maintain 5000, expect 10000 in 10 minutes."),
             "climb and maintain 5000 expect 10000 in 10 minutes.",
             "plain numbers untouched");

  // Assigned headings use standard ATC digit cadence instead of cardinal-number
  // speech ("two hundred forty").
  require_eq(speech_text("turn left heading 240."),
             "turn left heading two four zero.",
             "heading spoken digit-by-digit");
  require_eq(speech_text("fly heading 005, vector for spacing."),
             "fly heading zero zero five vector for spacing.",
             "leading heading zeros spoken");
  require_eq(speech_text("runway 240, maintain 2400."),
             "runway 240 maintain 2400.",
             "non-heading integer groups remain natural");

  // A decimal that is not frequency-shaped (integer part not 3 digits starting
  // with 1) is left alone.
  require_eq(speech_text("visibility 2.5, altimeter 29.92."),
             "visibility 2.5 altimeter 29.92.", "non-frequency decimals untouched");

  std::cout << "speech_text tests passed\n";
  return 0;
}
