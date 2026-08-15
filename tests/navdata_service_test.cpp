#include "zoal_atc/navdata/md5.hpp"
#include "zoal_atc/navdata/navdata_service.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace zoal_atc::navdata;
namespace fs = std::filesystem;

namespace {

void require(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

void require_contains(const std::string &haystack, const std::string &needle,
                      const std::string &message) {
  require(haystack.find(needle) != std::string::npos,
          message + " (missing `" + needle + "` in `" +
              haystack.substr(0, 200) + "`)");
}

// RFC 1321 test vectors — the hash must match the console's md5 exactly.
void md5_matches_rfc_vectors() {
  require(md5_hex("") == "d41d8cd98f00b204e9800998ecf8427e", "md5 empty");
  require(md5_hex("abc") == "900150983cd24fb0d6963f7d28e17f72", "md5 abc");
  require(md5_hex("message digest") == "f96b697d7cb7938d525a2f31aaf161d0",
          "md5 message digest");
  require(md5_hex("1234567890123456789012345678901234567890123456789012345678"
                  "9012345678901234567890") ==
              "57edf4a22be3c955ac49da2e2107b67a",
          "md5 80 digits (multi-block)");
}

const char kAptText[] = R"(I
1100 Generated

1 7 0 0 KPAO Palo Alto Airport
100 30.00 1 0 0.25 1 2 1 31 37.4539 -122.1093 0 0 2 0 0 0 13 37.4611 -122.1150 0 0 2 0 0 0
1054 118600 KPAO TWR

1 12 0 0 KSQL San Carlos Airport
100 23.00 1 0 0.25 1 2 1 12 37.5120 -122.2520 0 0 2 0 0 0 30 37.5200 -122.2450 0 0 2 0 0 0

1 32 0 0 KNUQ Moffett Federal Airfield
100 60.00 1 0 0.25 1 2 1 32L 37.4080 -122.0480 0 0 2 0 0 0 14R 37.4230 -122.0600 0 0 2 0 0 0
99
)";

const char kHelipadAptText[] = R"(I
1100 Generated

17 331 0 0 CPK7 Ottawa Children's Hospital
102 H1 37.4570 -122.1120 90.00 20.00 20.00 1 0 0 0.25 0

1 32 0 0 KNUQ Moffett Federal Airfield
100 60.00 1 0 0.25 1 2 1 32L 37.4080 -122.0480 0 0 2 0 0 0 14R 37.4230 -122.0600 0 0 2 0 0 0
1054 119550 KNUQ TWR
99
)";

const char kWaterRunwayAptText[] = R"(I
1100 Generated

16 190 0 0 CTR7 Rockcliffe Seaplane Base
101 49.00 1 09W 37.4570 -122.1120 27W 37.4580 -122.1120
1051 122800 CTR7 CTAF

1 32 0 0 KNUQ Moffett Federal Airfield
100 60.00 1 0 0.25 1 2 1 32L 37.4080 -122.0480 0 0 2 0 0 0 14R 37.4230 -122.0600 0 0 2 0 0 0
1054 119550 KNUQ TWR
99
)";

// The scoped extraction pulls only the wanted airport blocks — never the whole
// file — and stops each block at the next header or the 99 terminator.
void extracts_wanted_airport_blocks() {
  std::istringstream apt(kAptText);
  const std::string scoped = extract_airport_blocks(apt, {"kpao", "KNUQ"});

  require_contains(scoped, "KPAO Palo Alto Airport", "KPAO header");
  require_contains(scoped, "1054 118600 KPAO TWR", "KPAO freq row");
  require_contains(scoped, "KNUQ Moffett Federal Airfield", "KNUQ header");
  require(scoped.find("KSQL") == std::string::npos,
          "unwanted airport must not leak into the scoped text");
  require(scoped.find("\n99") == std::string::npos,
          "terminator row is not part of a block");
}

void extracts_regional_airport_blocks() {
  std::istringstream apt(kAptText);
  const std::string scoped =
      extract_regional_airport_blocks(apt, {"KPAO"}, 5.5);

  require_contains(scoped, "KPAO Palo Alto Airport", "regional includes base");
  require_contains(scoped, "KNUQ Moffett Federal Airfield",
                   "regional includes nearby field");
  require(scoped.find("KSQL San Carlos Airport") == std::string::npos,
          "regional radius excludes farther airport");

  std::istringstream tiny(kAptText);
  const std::string exact = extract_regional_airport_blocks(tiny, {"KPAO"}, 1);
  require_contains(exact, "KPAO Palo Alto Airport", "tiny includes base");
  require(exact.find("KNUQ") == std::string::npos,
          "tiny regional radius excludes nearby airport");
}

void regional_extraction_uses_helipad_reference_points() {
  std::istringstream apt(kHelipadAptText);
  const std::string scoped =
      extract_regional_airport_blocks(apt, {"CPK7"}, 5.5);

  require_contains(scoped, "CPK7 Ottawa Children's Hospital",
                   "regional includes heliport base");
  require_contains(scoped, "KNUQ Moffett Federal Airfield",
                   "regional around heliport includes nearby airport");
}

void regional_extraction_uses_water_runway_reference_points() {
  std::istringstream apt(kWaterRunwayAptText);
  const std::string scoped =
      extract_regional_airport_blocks(apt, {"CTR7"}, 5.5);

  require_contains(scoped, "CTR7 Rockcliffe Seaplane Base",
                   "regional includes seaplane base");
  require_contains(scoped, "KNUQ Moffett Federal Airfield",
                   "regional around seaplane base includes nearby airport");
}

// Frame builders must match the console decoder keys (type/icao/md5/source/text).
void frames_match_console_contract() {
  const std::string hash_frame =
      build_navdata_hash_frame(NavdataKind::Apt, "KPAO", "abc123", "custom");
  require_contains(hash_frame, "\"type\":\"navdata_apt_hash\"", "hash type");
  require_contains(hash_frame, "\"icao\":\"KPAO\"", "hash icao");
  require_contains(hash_frame, "\"md5\":\"abc123\"", "hash md5");
  require_contains(hash_frame, "\"source\":\"custom\"", "hash source");

  const std::string full = build_navdata_frame(
      NavdataKind::Cifp, "KPAO", "abc123", "global", "SID:010,2\nline two");
  require_contains(full, "\"type\":\"navdata_cifp\"", "full type");
  require_contains(full, "\"text\":\"SID:010,2\\nline two\"",
                   "text is JSON-escaped");
}

void parses_requests() {
  const auto scan = parse_navdata_request(
      R"({"type":"navdata_scan_request","icao":"CYOW"})");
  require(scan.has_value() && scan->scan && scan->icao == "CYOW",
          "destination scan request parses");
  require(!parse_navdata_request(R"({"type":"navdata_scan_request"})")
               .has_value(),
          "destination scan requires an ICAO");

  const auto apt = parse_navdata_request(
      R"({"type":"navdata_apt_request","icao":"KPAO","md5":"h1"})");
  require(apt.has_value() && apt->kind == NavdataKind::Apt &&
              apt->icao == "KPAO" && apt->md5 == "h1",
          "apt request parses");

  const auto vrp =
      parse_navdata_request(R"({"type":"navdata_vrp_request","md5":"h2"})");
  require(vrp.has_value() && vrp->kind == NavdataKind::Vrp && vrp->md5 == "h2",
          "vrp request parses");

  require(!parse_navdata_request(R"({"type":"atc_reply","text":"hi"})")
               .has_value(),
          "non-request frames are ignored");
}

struct TempRoot {
  fs::path root;

  TempRoot() {
    root = fs::temp_directory_path() / "zoal_atc_navdata_test_root";
    fs::remove_all(root);
    fs::create_directories(root);
  }
  ~TempRoot() { fs::remove_all(root); }

  void write(const fs::path &rel, const std::string &content) const {
    const fs::path full = root / rel;
    fs::create_directories(full.parent_path());
    std::ofstream out(full, std::ios::binary);
    out << content;
  }
};

// End to end: the publisher scans the files for an airport, sends one hash
// frame per (kind, source), and answers requests from the stored text.
void publisher_hash_first_round_trip() {
  TempRoot tmp;
  tmp.write(fs::path("Global Scenery") / "Global Airports" /
                "Earth nav data" / "apt.dat",
            kAptText);
  tmp.write(fs::path("Custom Scenery") / "KPAO_Custom" / "Earth nav data" /
                "apt.dat",
            "1 7 0 0 KPAO Palo Alto Custom\n"
            "1054 118600 KPAO TWR\n99\n");
  tmp.write(fs::path("Custom Data") / "CIFP" / "KPAO.dat",
            "SID:010,2,SJC1,RW31,SUNOL,K2,D,,,,,IF,,;\n");
  tmp.write(fs::path("Custom Data") / "airspaces" / "airspace.txt",
            "AC D\nAN PALO ALTO TOWER\nAL SFC\nAH 2500ft\n"
            "DP 37:30.00 N 122:10.00 W\nDP 37:30.00 N 122:00.00 W\n"
            "DP 37:24.00 N 122:00.00 W\n");
  tmp.write(fs::path("Custom Data") / "vrps.csv", "SLAC,37.42,-122.204\n");

  std::vector<std::string> sent;
  NavdataPublisher publisher(NavdataPaths{tmp.root.string()},
                             [&](const std::string &frame) {
                               sent.push_back(frame);
                             });

  publisher.publish_for_airport("KPAO");
  // Two apt sources (custom + global) + cifp + airspace + vrp = 5 hash frames.
  require(sent.size() == 5, "expected 5 hash frames, got " +
                                std::to_string(sent.size()));

  int custom_apt = 0;
  int global_apt = 0;
  for (const auto &frame : sent) {
    if (frame.find("navdata_apt_hash") == std::string::npos) {
      continue;
    }
    if (frame.find("\"source\":\"custom\"") != std::string::npos) {
      ++custom_apt;
    }
    if (frame.find("\"source\":\"global\"") != std::string::npos) {
      ++global_apt;
    }
  }
  require(custom_apt == 1 && global_apt == 1,
          "one custom and one global apt hash frame");

  // A request for the CIFP hash returns the full text.
  std::string cifp_hash;
  for (const auto &frame : sent) {
    if (frame.find("navdata_cifp_hash") != std::string::npos) {
      const auto pos = frame.find("\"md5\":\"");
      constexpr std::size_t kPrefixLen = 7; // `"md5":"`
      cifp_hash = frame.substr(pos + kPrefixLen, 32);
    }
  }
  require(!cifp_hash.empty(), "cifp hash frame present");

  sent.clear();
  NavdataRequest request;
  request.kind = NavdataKind::Cifp;
  request.icao = "KPAO";
  request.md5 = cifp_hash;
  require(publisher.handle_request(request), "request answered");
  require(sent.size() == 1, "one full frame sent");
  require_contains(sent[0], "\"type\":\"navdata_cifp\"", "full cifp frame");
  require_contains(sent[0], "SJC1", "cifp text present");

  request.md5 = "unknown";
  require(!publisher.handle_request(request), "unknown hash is not answered");
}

void paths_resolve_from_plugin_subdirectory() {
  TempRoot tmp;
  tmp.write(fs::path("Global Scenery") / "Global Airports" /
                "Earth nav data" / "apt.dat",
            kAptText);
  const fs::path plugin_dir =
      tmp.root / "Resources" / "plugins" / "zoal-atc" / "mac_x64";
  fs::create_directories(plugin_dir);

  NavdataPaths paths{plugin_dir.string()};
  require(paths.resolved_root() == tmp.root.string(),
          "plugin subdirectory resolves to X-Plane root");
  const auto candidates = paths.apt_candidates();
  require(candidates.size() == 1, "global apt.dat found from resolved root");
  require(candidates[0].find("Global Airports") != std::string::npos,
          "resolved candidate is the global apt.dat");
}

void metar_newest_file_and_frame() {
  TempRoot tmp;
  tmp.write(fs::path("Output") / "real weather" / "metar-2026-07-03-11.txt",
            "KPAO 031100Z 29009KT 10SM FEW015 18/12 A2998\n");
  tmp.write(fs::path("Output") / "real weather" / "notes.txt", "ignore me");

  NavdataPaths paths{tmp.root.string()};
  const auto newest = newest_metar_file(paths.metar_dir());
  require(newest.has_value(), "newest metar found");
  require(newest->find("metar-2026-07-03-11.txt") != std::string::npos,
          "picked the metar file, not other files");

  const auto text = read_file_text(*newest);
  require(text.has_value(), "metar readable");
  const std::string frame = build_metar_frame(*text);
  require_contains(frame, "\"type\":\"weather_metar\"", "metar frame type");
  require_contains(frame, "KPAO 031100Z", "metar text");

  require(!newest_metar_file((tmp.root / "nope").string()).has_value(),
          "missing dir yields no metar");
}

// Phase7 M1: the bounding-box row filter over the global fix files keeps
// nearby rows, drops distant ones, and skips headers/terminators.
void extracts_fix_rows_in_box() {
  std::istringstream fix(
      "I\n1101 Version - data cycle 2306\n"
      "37.505700 -122.291500 DOCAL KSFO K2 4530243\n"
      "51.000000 0.500000 FARAW ENRT EG 4530243\n"
      "99\n");
  const std::string near = extract_rows_in_box(fix, 0, 37.46, -122.11, 150.0);
  require_contains(near, "DOCAL", "nearby fix kept");
  require(near.find("FARAW") == std::string::npos, "distant fix dropped");
  require(near.find("1101") == std::string::npos, "header row dropped");
  require(near.find("99") == std::string::npos, "terminator dropped");

  // earth_nav.dat rows carry the coordinates one field later (type first).
  std::istringstream nav(
      "I\n1150 Version\n"
      "3  37.619483 -122.373892    13 11590   130   17.0 SFO ENRT K2 SAN "
      "FRANCISCO VORTAC\n"
      "99\n");
  const std::string near_nav =
      extract_rows_in_box(nav, 1, 37.46, -122.11, 150.0);
  require_contains(near_nav, "SFO", "nav row kept with lat field 1");
}

void fixes_frames_and_requests() {
  const std::string hash = build_fixes_hash_frame("fix", "abc123", "custom");
  require_contains(hash, "\"type\":\"navdata_fixes_hash\"", "fixes hash type");
  require_contains(hash, "\"file\":\"fix\"", "fixes hash file tag");
  require_contains(hash, "\"source\":\"custom\"", "fixes hash source");

  const std::string full =
      build_fixes_frame("nav", "abc123", "global", "3 37.6 -122.4 ... SFO");
  require_contains(full, "\"type\":\"navdata_fixes\"", "fixes frame type");
  require_contains(full, "\"file\":\"nav\"", "fixes frame file tag");
  require_contains(full, "SFO", "fixes frame text");

  const auto request = parse_navdata_request(
      "{\"type\":\"navdata_fixes_request\",\"file\":\"nav\",\"md5\":\"abc\"}");
  require(request.has_value(), "fixes request parses");
  require(request->kind == NavdataKind::Fixes, "fixes request kind");
  require(request->file == "nav" && request->md5 == "abc",
          "fixes request file + md5");
}

// End to end: the publisher scopes the global fix files to a box around the
// session airport (custom AIRAC beating default data) and answers the
// hash-first request with the scoped text.
void publisher_ships_scoped_fixes() {
  TempRoot tmp;
  tmp.write(fs::path("Global Scenery") / "Global Airports" /
                "Earth nav data" / "apt.dat",
            kAptText);
  tmp.write(fs::path("Custom Data") / "earth_fix.dat",
            "I\n1101 Version - data cycle 2306\n"
            "37.505700 -122.291500 DOCAL KSFO K2 4530243\n"
            "51.000000 0.500000 FARAW ENRT EG 4530243\n"
            "99\n");
  tmp.write(fs::path("Resources") / "default data" / "earth_nav.dat",
            "I\n1150 Version\n"
            "3  37.619483 -122.373892 13 11590 130 17.0 SFO ENRT K2 SAN "
            "FRANCISCO VORTAC\n"
            "99\n");

  std::vector<std::string> sent;
  NavdataPublisher publisher(
      NavdataPaths{tmp.root.string()},
      [&](const std::string &frame) { sent.push_back(frame); });
  publisher.publish_for_airport("KPAO");

  std::string fix_hash;
  bool nav_hash_global = false;
  for (const auto &frame : sent) {
    if (frame.find("navdata_fixes_hash") == std::string::npos) {
      continue;
    }
    if (frame.find("\"file\":\"fix\"") != std::string::npos) {
      require_contains(frame, "\"source\":\"custom\"",
                       "custom earth_fix wins over default data");
      const auto pos = frame.find("\"md5\":\"");
      constexpr std::size_t kPrefixLen = 7; // `"md5":"`
      fix_hash = frame.substr(pos + kPrefixLen, 32);
    }
    if (frame.find("\"file\":\"nav\"") != std::string::npos &&
        frame.find("\"source\":\"global\"") != std::string::npos) {
      nav_hash_global = true;
    }
  }
  require(!fix_hash.empty(), "earth_fix hash frame present");
  require(nav_hash_global, "earth_nav hash frame present (default data)");

  sent.clear();
  NavdataRequest request;
  request.kind = NavdataKind::Fixes;
  request.file = "fix";
  request.md5 = fix_hash;
  require(publisher.handle_request(request), "fixes request answered");
  require(sent.size() == 1, "one full fixes frame sent");
  require_contains(sent[0], "\"type\":\"navdata_fixes\"", "full fixes frame");
  require_contains(sent[0], "\"file\":\"fix\"", "full frame file tag");
  require_contains(sent[0], "DOCAL", "nearby fix in the scoped text");
  require(sent[0].find("FARAW") == std::string::npos,
          "distant fix stays out of the scoped text");
}

} // namespace

// The atc.dat kind rides the generic frame/request path (phase11 G4): a
// navdata_atc_hash / navdata_atc frame pair and a navdata_atc_request that parses
// to NavdataKind::Atc.
void atc_frames_and_requests() {
  require_contains(build_navdata_hash_frame(NavdataKind::Atc, "", "h9", "global"),
                   "\"type\":\"navdata_atc_hash\"", "atc hash frame type");
  const std::string full = build_navdata_frame(NavdataKind::Atc, "", "h9",
                                               "global", "CONTROLLER\nNAME MONTREAL");
  require_contains(full, "\"type\":\"navdata_atc\"", "atc frame type");
  require_contains(full, "MONTREAL", "atc frame text");

  const auto request = parse_navdata_request(
      "{\"type\":\"navdata_atc_request\",\"md5\":\"h9\"}");
  require(request.has_value() && request->kind == NavdataKind::Atc &&
              request->md5 == "h9",
          "atc request parses to Atc kind");
}

int main() {
  md5_matches_rfc_vectors();
  extracts_wanted_airport_blocks();
  extracts_regional_airport_blocks();
  regional_extraction_uses_helipad_reference_points();
  regional_extraction_uses_water_runway_reference_points();
  frames_match_console_contract();
  parses_requests();
  publisher_hash_first_round_trip();
  paths_resolve_from_plugin_subdirectory();
  metar_newest_file_and_frame();
  extracts_fix_rows_in_box();
  fixes_frames_and_requests();
  publisher_ships_scoped_fixes();
  atc_frames_and_requests();
  std::cout << "zoal_atc_navdata_tests: all tests passed\n";
  return 0;
}
