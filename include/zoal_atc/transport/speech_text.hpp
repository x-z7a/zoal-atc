#ifndef ZOAL_ATC_TRANSPORT_SPEECH_TEXT_HPP
#define ZOAL_ATC_TRANSPORT_SPEECH_TEXT_HPP

#include <string>
#include <string_view>

namespace zoal_atc::transport {

// radio_text is a reply as the controller's voice should read it. The text is
// written for the eye; frequencies and assigned headings are expanded to digit
// cadence ("one three three point four", "heading two four zero") so they are
// not read as numbers, and punctuation is kept, because a neural voice pauses
// at a comma and that pause is where a controller breathes. Altitudes,
// visibilities and altimeter settings are left to the voice's natural reading.
std::string radio_text(std::string_view reply);

}  // namespace zoal_atc::transport

#endif  // ZOAL_ATC_TRANSPORT_SPEECH_TEXT_HPP
