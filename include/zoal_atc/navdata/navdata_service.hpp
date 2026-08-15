#ifndef ZOAL_ATC_NAVDATA_NAVDATA_SERVICE_HPP
#define ZOAL_ATC_NAVDATA_NAVDATA_SERVICE_HPP

#include <functional>
#include <istream>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The navdata half of the plugin edge (docs/data-sources.md): the plugin reads
// the local files natively — apt.dat (global + custom scenery overrides),
// Custom Data/CIFP/{ICAO}.dat, the OpenAir airspace file, the VRP database —
// and ships raw text over the WebSocket; the console parses. Transfers are
// hash-first: only the md5 goes out initially, and the full text is sent only
// when the console misses its persisted parse cache and asks
// (navdata_*_request). Everything here is SDK-free and testable; the X-Plane
// root path comes from the SDK glue.
namespace zoal_atc::navdata {

// NavdataKind names the file kinds; values match the console's frame types
// (navdata_<kind>, navdata_<kind>_hash, navdata_<kind>_request). Fixes covers
// both global fix files (earth_fix.dat / earth_nav.dat, phase7 M1); the frame's
// "file" field says which ("fix" | "nav").
enum class NavdataKind { Apt, Cifp, Airspace, Vrp, Fixes, Atc };

// kind_name returns "apt" / "cifp" / "airspace" / "vrp" / "fixes".
std::string_view kind_name(NavdataKind kind);

// extract_airport_blocks scans apt.dat text for the airport blocks whose ICAO
// is in wanted (header rows 1/16/17, identifier at field 4) and returns them
// concatenated — the scoped text sent to the console (never the whole
// multi-hundred-MB file). Streaming: one pass, line by line.
std::string extract_airport_blocks(std::istream &apt,
                                   const std::vector<std::string> &wanted);

// extract_regional_airport_blocks returns the wanted airport blocks plus any
// airport blocks whose reference point is within radius_nm of a wanted airport.
// It still streams one block at a time and is used to send a terminal-area
// summary index without shipping the full apt.dat.
std::string extract_regional_airport_blocks(
    std::istream &apt, const std::vector<std::string> &wanted,
    double radius_nm);

// extract_rows_in_box scans a line-oriented coordinate file (earth_fix.dat /
// earth_nav.dat) and returns the rows whose lat/lon — at whitespace fields
// lat_field and lat_field+1 — fall within radius_nm of (lat, lon). Header and
// terminator rows fail the coordinate parse and are skipped. Streaming, one
// pass; a box filter is data plumbing, not ATC logic (phase7 M1).
std::string extract_rows_in_box(std::istream &in, std::size_t lat_field,
                                double lat, double lon, double radius_nm);

// find_airport_reference scans apt.dat text for icao's block and returns its
// reference point, or nullopt when the airport is absent/unlocated.
std::optional<std::pair<double, double>> find_airport_reference(
    std::istream &apt, const std::string &icao);

// build_navdata_hash_frame / build_navdata_frame render the wire frames. Keys
// match the console decoder (type/icao/md5/source/text).
std::string build_navdata_hash_frame(NavdataKind kind, std::string_view icao,
                                     std::string_view md5,
                                     std::string_view source);
std::string build_navdata_frame(NavdataKind kind, std::string_view icao,
                                std::string_view md5, std::string_view source,
                                std::string_view text);

// build_fixes_hash_frame / build_fixes_frame render the navdata_fixes frames,
// which carry a "file" field ("fix" | "nav") instead of an ICAO.
std::string build_fixes_hash_frame(std::string_view file, std::string_view md5,
                                   std::string_view source);
std::string build_fixes_frame(std::string_view file, std::string_view md5,
                              std::string_view source, std::string_view text);

// NavdataRequest is a parsed console navdata_*_request frame.
struct NavdataRequest {
  NavdataKind kind = NavdataKind::Apt;
  // scan asks the plugin worker to publish hash offers for icao instead of
  // looking up one already-offered hash.
  bool scan = false;
  std::string icao;
  std::string md5;
  // file scopes a Fixes request to one global file ("fix" | "nav").
  std::string file;
};

// parse_navdata_request decodes a navdata_*_request payload, or nullopt for
// any other frame type.
std::optional<NavdataRequest> parse_navdata_request(const std::string &payload);

// NavdataPaths composes the well-known file locations under an X-Plane root.
struct NavdataPaths {
  std::string xplane_root;

  // resolved_root returns xplane_root when it already looks like the X-Plane
  // install root, otherwise walks upward until it finds the install root. This
  // keeps the plugin resilient if the SDK supplies a path inside the bundle or
  // plugin tree on a platform.
  std::string resolved_root() const;

  // apt_candidates returns custom-scenery apt.dat files first (overrides),
  // then the global one — only paths that exist. Sources align by index.
  std::vector<std::string> apt_candidates() const;
  std::vector<std::string> apt_sources() const;

  std::string cifp_path(std::string_view icao) const;
  std::string airspace_path() const;
  std::string atc_path() const;
  std::string vrp_path() const;
  // fix_candidates returns the existing locations of one global fix file
  // ("fix" = earth_fix.dat, "nav" = earth_nav.dat): Custom Data (updated AIRAC)
  // first, then Resources/default data. Sources align by index
  // ("custom"/"global").
  std::vector<std::string> fix_candidates(std::string_view file) const;
  std::vector<std::string> fix_sources(std::string_view file) const;
  // metar_dir is where X-Plane downloads real-weather METAR files.
  std::string metar_dir() const;
};

// NavdataPublisher drives the hash-first exchange. It loads/scopes the files
// for an airport, sends the hash frames through the injected sender, keeps the
// full text keyed by hash, and answers requests from that store. Not
// thread-safe on its own — drive it from one worker thread.
class NavdataPublisher {
public:
  using Sender = std::function<void(const std::string &frame)>;
  using DiagnosticSink = std::function<void(const std::string &message)>;

  NavdataPublisher(NavdataPaths paths, Sender sender,
                   DiagnosticSink diagnostics = {});

  // publish_for_airport scans the files for icao and sends one hash frame per
  // available (kind, source) pair. The apt.dat payload is scoped to a bounded
  // regional slice around icao so the console can build nearby-airport/frequency
  // context without receiving the whole file. Missing files are silently skipped
  // — graceful degradation is console-side policy.
  void publish_for_airport(const std::string &icao);

  // handle_request answers a console navdata_*_request with the stored full
  // text. Returns false when the hash is unknown (nothing sent).
  bool handle_request(const NavdataRequest &request);

private:
  struct Entry {
    NavdataKind kind;
    std::string icao;
    std::string source;
    std::string text;
    std::string file; // Fixes only: "fix" | "nav"
  };

  void offer(NavdataKind kind, const std::string &icao,
             const std::string &source, std::string text);
  void offer_fixes(const std::string &file, const std::string &source,
                   std::string text);
  void publish_fixes(double lat, double lon);

  NavdataPaths paths_;
  Sender sender_;
  DiagnosticSink diagnostics_;
  std::map<std::string, Entry> by_hash_;
};

// read_file_text reads a whole file, or nullopt when it cannot be opened.
std::optional<std::string> read_file_text(const std::string &path);

// newest_metar_file returns the most recently modified metar-*.txt in dir, or
// nullopt when none exists.
std::optional<std::string> newest_metar_file(const std::string &dir);

// build_metar_frame renders the weather_metar frame from the METAR file text.
std::string build_metar_frame(std::string_view text);

} // namespace zoal_atc::navdata

#endif // ZOAL_ATC_NAVDATA_NAVDATA_SERVICE_HPP
