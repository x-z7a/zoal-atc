#include "zoal_atc/gui/display_numbers.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

using zoal_atc::gui::display_numbers;

namespace {

int failures = 0;

void expect(const std::string &in, const std::string &want) {
  const std::string got = display_numbers(in);
  if (got != want) {
    std::cerr << "FAIL\n  in:   `" << in << "`\n  got:  `" << got
              << "`\n  want: `" << want << "`\n";
    ++failures;
  }
}

}  // namespace

int main() {
  // Digit runs, with the punctuation after them kept.
  expect("Delta 141, turn left heading two seven zero, contact departure one "
         "two five point three five.",
         "Delta 141, turn left heading 270, contact departure 125.35.");
  expect("November One Two, departure frequency one three three point four.",
         "November 12, departure frequency 133.4.");
  expect("Air Canada two one, ACA21.", "Air Canada 21, ACA21.");
  expect("flight level three five zero", "flight level 350");
  expect("squawk four five two one", "squawk 4521");
  expect("runway zero seven", "runway 07");
  expect("one one niner point zero", "119.0");
  expect("tree fife", "35");

  // Thousands and hundreds are their value.
  expect("Climb and maintain five thousand. Squawk 4521.",
         "Climb and maintain 5000. Squawk 4521.");
  expect("climb and maintain three thousand five hundred",
         "climb and maintain 3500");
  expect("descend to one zero thousand", "descend to 10000");
  expect("five hundred feet", "500 feet");
  expect("one one thousand five hundred", "11500");

  // Spanish.
  expect("Air Canada cuatro cero nueve seis, ruede a pista uno cinco derecha",
         "Air Canada 4096, ruede a pista 15 derecha");
  expect("Yankee, Echo, Hotel tres, Papa, Juliett, mantenga fuera de la pista "
         "dos tres.",
         "Yankee, Echo, Hotel tres, Papa, Juliett, mantenga fuera de la pista 23.");
  expect("ascienda a tres mil quinientos pies", "ascienda a 3500 pies");
  expect("quinientos pies", "500 pies");
  expect("frecuencia uno dos uno punto nueve", "frecuencia 121.9");

  // French.
  expect("montez cinq mille pieds", "montez 5000 pieds");
  expect("trois mille cinq cents", "3500");
  expect("piste deux sept, contactez un un huit virgule sept",
         "piste 27, contactez 118.7");
  expect("Zéro neuf", "09");

  // A lone digit word stays a word, unless the word before takes a number.
  expect("Delta un, reçu. Restez à l'écoute.", "Delta un, reçu. Restez à l'écoute.");
  expect("Two pilots connected to one console", "Two pilots connected to one console");
  expect("a new one replaces it at once", "a new one replaces it at once");
  expect("runway two", "runway 2");
  expect("Runway two, cleared", "Runway 2, cleared");
  expect("rumbo cinco", "rumbo 5");
  expect("heading, two", "heading, two");

  // Punctuation ends a number.
  expect("two, three", "two, three");
  expect("one two, three four", "12, 34");
  expect("(one two)", "(12)");

  // A point with no digits after it is a word; a multiplier with no digits
  // before it is a word.
  expect("one two five point", "125 point");
  expect("thousand feet", "thousand feet");
  expect("mil gracias", "mil gracias");

  // Everything else is copied byte for byte.
  expect("", "");
  expect("  roger   wilco  ", "  roger   wilco  ");
  expect("Climb  two  four", "Climb  24");
  expect("Squawk 4521.", "Squawk 4521.");

  if (failures != 0) {
    std::cerr << failures << " failure(s)\n";
    return EXIT_FAILURE;
  }
  std::cout << "display_numbers: all passed\n";
  return EXIT_SUCCESS;
}
