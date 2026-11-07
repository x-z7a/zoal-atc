#ifndef ZOAL_ATC_WEATHER_METAR_HPP
#define ZOAL_ATC_WEATHER_METAR_HPP

#include <optional>
#include <string>
#include <string_view>

// The sim's own weather: X-Plane downloads real-weather METAR files into
// Output/real weather, and the plugin forwards the newest one so the console
// hears the same weather the pilot is flying in. Everything here is SDK-free.
namespace zoal_atc::weather {

// resolve_xplane_root returns path when it already looks like the X-Plane
// install root, otherwise walks upward until it finds one. This keeps the
// plugin resilient if the SDK supplies a path inside the bundle or plugin tree.
std::string resolve_xplane_root(const std::string &path);

// metar_dir is where X-Plane downloads real-weather METAR files.
std::string metar_dir(const std::string &xplane_root);

// read_file_text reads a whole file, or nullopt when it cannot be opened.
std::optional<std::string> read_file_text(const std::string &path);

// newest_metar_file returns the most recently modified metar-*.txt in dir, or
// nullopt when there is none.
std::optional<std::string> newest_metar_file(const std::string &dir);

// build_metar_frame renders the weather_metar frame from the METAR file text.
std::string build_metar_frame(std::string_view text);

} // namespace zoal_atc::weather

#endif // ZOAL_ATC_WEATHER_METAR_HPP
