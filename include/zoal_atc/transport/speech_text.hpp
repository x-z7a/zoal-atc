#ifndef ZOAL_ATC_TRANSPORT_SPEECH_TEXT_HPP
#define ZOAL_ATC_TRANSPORT_SPEECH_TEXT_HPP

#include <string>
#include <string_view>

namespace zoal_atc::transport {

// speech_text normalizes an ATC reply for XPLMSpeakString. The reply text is
// written for the eye (and future console-side TTS, which wants the commas for
// prosody), so it carries punctuation the X-Plane speech synthesizer would read
// aloud literally -- "taxi via Tango, continue onto Quebec" is voiced
// "... Quebec comma continue". This strips the punctuation the synth vocalizes
// (commas, semicolons, colons) down to a plain word gap and collapses the
// resulting whitespace, leaving sentence-terminating periods alone. Radio
// frequencies and three-digit values following "heading" are expanded into
// individual digit words for standard ATC cadence.
std::string speech_text(std::string_view reply);

// radio_text is the same reading for a synthesiser that pauses at a comma
// rather than saying it: frequencies and headings digit by digit, and the
// punctuation kept, because it is where a controller breathes.
std::string radio_text(std::string_view reply);

}  // namespace zoal_atc::transport

#endif  // ZOAL_ATC_TRANSPORT_SPEECH_TEXT_HPP
