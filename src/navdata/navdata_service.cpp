#include "zoal_atc/navdata/navdata_service.hpp"

#include "zoal_atc/navdata/md5.hpp"
#include "zoal_atc/transport/json_util.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <utility>

namespace zoal_atc::navdata {

namespace fs = std::filesystem;
using transport::json_escape;
using transport::json_string_field;

namespace {

std::string to_upper(std::string_view value) {
  std::string out(value);
  std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
    return static_cast<char>(std::toupper(c));
  });
  return out;
}

// header_icao returns the ICAO of an apt.dat airport header row (row codes
// 1/16/17, identifier at whitespace field 4), or empty for any other row.
std::string header_icao(const std::string &line) {
  std::istringstream fields(line);
  std::string code;
  fields >> code;
  if (code != "1" && code != "16" && code != "17") {
    return "";
  }
  std::string skip;
  fields >> skip >> skip; // elevation, deprecated flags
  std::string icao;
  fields >> skip >> icao; // tower flag, then the identifier
  return to_upper(icao);
}

bool is_terminator(const std::string &line) {
  std::istringstream fields(line);
  std::string code;
  fields >> code;
  return code == "99";
}

std::vector<std::string> line_fields(const std::string &line) {
  std::vector<std::string> out;
  std::istringstream fields(line);
  std::string field;
  while (fields >> field) {
    out.push_back(field);
  }
  return out;
}

double double_field(const std::vector<std::string> &fields,
                    std::size_t index) {
  if (index >= fields.size()) {
    return 0.0;
  }
  std::istringstream in(fields[index]);
  double value = 0.0;
  in >> value;
  return value;
}

bool valid_lat_lon(double lat, double lon) {
  return (lat != 0.0 || lon != 0.0) && lat >= -90.0 && lat <= 90.0 &&
         lon >= -180.0 && lon <= 180.0;
}

struct AirportBlock {
  std::string icao;
  std::string text;
  double lat = 0.0;
  double lon = 0.0;
  bool located = false;
};

void update_block_reference(AirportBlock &block,
                            const std::vector<std::string> &fields) {
  if (block.located || fields.empty()) {
    return;
  }
  const std::string &code = fields[0];
  if (code == "100" && fields.size() > 19) {
    const double lat1 = double_field(fields, 9);
    const double lon1 = double_field(fields, 10);
    const double lat2 = double_field(fields, 18);
    const double lon2 = double_field(fields, 19);
    if (valid_lat_lon(lat1, lon1) || valid_lat_lon(lat2, lon2)) {
      block.lat = (lat1 + lat2) / 2.0;
      block.lon = (lon1 + lon2) / 2.0;
      block.located = true;
    }
    return;
  }
  if (code == "101" && fields.size() > 8) {
    const double lat1 = double_field(fields, 4);
    const double lon1 = double_field(fields, 5);
    const double lat2 = double_field(fields, 7);
    const double lon2 = double_field(fields, 8);
    if (valid_lat_lon(lat1, lon1) && valid_lat_lon(lat2, lon2)) {
      block.lat = (lat1 + lat2) / 2.0;
      block.lon = (lon1 + lon2) / 2.0;
      block.located = true;
    }
    return;
  }
  if ((code == "1201" || code == "1300") && fields.size() > 2) {
    const double lat = double_field(fields, 1);
    const double lon = double_field(fields, 2);
    if (valid_lat_lon(lat, lon)) {
      block.lat = lat;
      block.lon = lon;
      block.located = true;
    }
    return;
  }
  if (code == "102") {
    for (const auto pair : {std::pair<std::size_t, std::size_t>{2, 3},
                            std::pair<std::size_t, std::size_t>{1, 2}}) {
      const double lat = double_field(fields, pair.first);
      const double lon = double_field(fields, pair.second);
      if (!valid_lat_lon(lat, lon)) {
        continue;
      }
      block.lat = lat;
      block.lon = lon;
      block.located = true;
      return;
    }
  }
}

void scan_airport_blocks(
    std::istream &apt,
    const std::function<void(const AirportBlock &)> &callback) {
  AirportBlock current;
  bool active = false;

  auto flush = [&]() {
    if (active && !current.icao.empty()) {
      callback(current);
    }
    current = AirportBlock{};
    active = false;
  };

  std::string line;
  while (std::getline(apt, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    const std::string icao = header_icao(line);
    if (!icao.empty()) {
      flush();
      current.icao = icao;
      current.text = line;
      current.text += '\n';
      active = true;
      continue;
    }
    if (is_terminator(line)) {
      flush();
      continue;
    }
    if (!active) {
      continue;
    }
    current.text += line;
    current.text += '\n';
    update_block_reference(current, line_fields(line));
  }
  flush();
}

bool contains_icao(const std::vector<std::string> &icaos,
                   const std::string &icao) {
  return std::find(icaos.begin(), icaos.end(), icao) != icaos.end();
}

bool rewind_stream(std::istream &apt) {
  apt.clear();
  apt.seekg(0, std::ios::beg);
  return static_cast<bool>(apt);
}

double deg_to_rad(double deg) { return deg * 3.14159265358979323846 / 180.0; }

double distance_nm(double lat1, double lon1, double lat2, double lon2) {
  constexpr double kEarthRadiusM = 6371000.0;
  constexpr double kMetersPerNm = 1852.0;
  const double p1 = deg_to_rad(lat1);
  const double p2 = deg_to_rad(lat2);
  const double dp = deg_to_rad(lat2 - lat1);
  const double dl = deg_to_rad(lon2 - lon1);
  const double sin_dp = std::sin(dp / 2.0);
  const double sin_dl = std::sin(dl / 2.0);
  const double raw_a = sin_dp * sin_dp +
                       std::cos(p1) * std::cos(p2) * sin_dl * sin_dl;
  const double a = std::max(0.0, std::min(1.0, raw_a));
  return (kEarthRadiusM * 2.0 * std::atan2(std::sqrt(a), std::sqrt(1.0 - a))) /
         kMetersPerNm;
}

bool within_radius_of_any(const AirportBlock &block,
                          const std::vector<AirportBlock> &refs,
                          double radius_nm) {
  if (!block.located) {
    return false;
  }
  for (const auto &ref : refs) {
    if (ref.located &&
        distance_nm(ref.lat, ref.lon, block.lat, block.lon) <= radius_nm) {
      return true;
    }
  }
  return false;
}

bool looks_like_xplane_root(const fs::path &root) {
  std::error_code ec;
  return fs::is_directory(root / "Global Scenery", ec) ||
         fs::is_directory(root / "Custom Data", ec) ||
         fs::is_directory(root / "Output", ec);
}

} // namespace

std::string_view kind_name(NavdataKind kind) {
  switch (kind) {
  case NavdataKind::Apt:
    return "apt";
  case NavdataKind::Cifp:
    return "cifp";
  case NavdataKind::Airspace:
    return "airspace";
  case NavdataKind::Vrp:
    return "vrp";
  case NavdataKind::Fixes:
    return "fixes";
  case NavdataKind::Atc:
    return "atc";
  }
  return "apt";
}

std::string extract_rows_in_box(std::istream &in, std::size_t lat_field,
                                double lat, double lon, double radius_nm) {
  std::string out;
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    const auto fields = line_fields(line);
    if (fields.size() <= lat_field + 1) {
      continue;
    }
    const double row_lat = double_field(fields, lat_field);
    const double row_lon = double_field(fields, lat_field + 1);
    if (!valid_lat_lon(row_lat, row_lon)) {
      continue; // headers, terminators, malformed rows
    }
    if (distance_nm(lat, lon, row_lat, row_lon) > radius_nm) {
      continue;
    }
    out += line;
    out += '\n';
  }
  return out;
}

std::optional<std::pair<double, double>> find_airport_reference(
    std::istream &apt, const std::string &icao) {
  const std::string wanted = to_upper(icao);
  std::optional<std::pair<double, double>> found;
  scan_airport_blocks(apt, [&](const AirportBlock &block) {
    if (!found.has_value() && block.icao == wanted && block.located) {
      found = std::make_pair(block.lat, block.lon);
    }
  });
  return found;
}

std::string extract_airport_blocks(std::istream &apt,
                                   const std::vector<std::string> &wanted) {
  std::vector<std::string> targets;
  targets.reserve(wanted.size());
  for (const auto &icao : wanted) {
    targets.push_back(to_upper(icao));
  }

  std::string out;
  scan_airport_blocks(apt, [&](const AirportBlock &block) {
    if (contains_icao(targets, block.icao)) {
      out += block.text;
    }
  });
  return out;
}

std::string extract_regional_airport_blocks(
    std::istream &apt, const std::vector<std::string> &wanted,
    double radius_nm) {
  std::vector<std::string> targets;
  targets.reserve(wanted.size());
  for (const auto &icao : wanted) {
    targets.push_back(to_upper(icao));
  }

  std::string exact;
  std::vector<AirportBlock> refs;
  scan_airport_blocks(apt, [&](const AirportBlock &block) {
    if (!contains_icao(targets, block.icao)) {
      return;
    }
    exact += block.text;
    if (block.located) {
      refs.push_back(block);
    }
  });
  if (refs.empty() || radius_nm <= 0.0 || !rewind_stream(apt)) {
    return exact;
  }

  std::string out;
  scan_airport_blocks(apt, [&](const AirportBlock &block) {
    if (contains_icao(targets, block.icao) ||
        within_radius_of_any(block, refs, radius_nm)) {
      out += block.text;
    }
  });
  if (out.empty()) {
    return exact;
  }
  return out;
}

std::string build_navdata_hash_frame(NavdataKind kind, std::string_view icao,
                                     std::string_view md5,
                                     std::string_view source) {
  std::ostringstream out;
  out << "{\"type\":\"navdata_" << kind_name(kind) << "_hash\"";
  if (!icao.empty()) {
    out << ",\"icao\":\"" << json_escape(icao) << "\"";
  }
  out << ",\"md5\":\"" << json_escape(md5) << "\"";
  if (!source.empty()) {
    out << ",\"source\":\"" << json_escape(source) << "\"";
  }
  out << "}";
  return out.str();
}

std::string build_navdata_frame(NavdataKind kind, std::string_view icao,
                                std::string_view md5, std::string_view source,
                                std::string_view text) {
  std::ostringstream out;
  out << "{\"type\":\"navdata_" << kind_name(kind) << "\"";
  if (!icao.empty()) {
    out << ",\"icao\":\"" << json_escape(icao) << "\"";
  }
  out << ",\"md5\":\"" << json_escape(md5) << "\"";
  if (!source.empty()) {
    out << ",\"source\":\"" << json_escape(source) << "\"";
  }
  out << ",\"text\":\"" << json_escape(text) << "\"}";
  return out.str();
}

std::string build_fixes_hash_frame(std::string_view file, std::string_view md5,
                                   std::string_view source) {
  std::ostringstream out;
  out << "{\"type\":\"navdata_fixes_hash\",\"file\":\"" << json_escape(file)
      << "\",\"md5\":\"" << json_escape(md5) << "\"";
  if (!source.empty()) {
    out << ",\"source\":\"" << json_escape(source) << "\"";
  }
  out << "}";
  return out.str();
}

std::string build_fixes_frame(std::string_view file, std::string_view md5,
                              std::string_view source, std::string_view text) {
  std::ostringstream out;
  out << "{\"type\":\"navdata_fixes\",\"file\":\"" << json_escape(file)
      << "\",\"md5\":\"" << json_escape(md5) << "\"";
  if (!source.empty()) {
    out << ",\"source\":\"" << json_escape(source) << "\"";
  }
  out << ",\"text\":\"" << json_escape(text) << "\"}";
  return out.str();
}

std::optional<NavdataRequest> parse_navdata_request(
    const std::string &payload) {
  const auto type = json_string_field(payload, "type");
  if (!type.has_value()) {
    return std::nullopt;
  }
  NavdataRequest request;
  if (*type == "navdata_scan_request") {
    request.scan = true;
  } else if (*type == "navdata_apt_request") {
    request.kind = NavdataKind::Apt;
  } else if (*type == "navdata_cifp_request") {
    request.kind = NavdataKind::Cifp;
  } else if (*type == "navdata_airspace_request") {
    request.kind = NavdataKind::Airspace;
  } else if (*type == "navdata_vrp_request") {
    request.kind = NavdataKind::Vrp;
  } else if (*type == "navdata_fixes_request") {
    request.kind = NavdataKind::Fixes;
  } else if (*type == "navdata_atc_request") {
    request.kind = NavdataKind::Atc;
  } else {
    return std::nullopt;
  }
  request.icao = json_string_field(payload, "icao").value_or("");
  if (request.scan && request.icao.empty()) {
    return std::nullopt;
  }
  request.md5 = json_string_field(payload, "md5").value_or("");
  request.file = json_string_field(payload, "file").value_or("");
  return request;
}

std::string NavdataPaths::resolved_root() const {
  fs::path root(xplane_root);
  if (root.empty()) {
    return xplane_root;
  }
  std::error_code ec;
  if (fs::is_regular_file(root, ec)) {
    root = root.parent_path();
  }
  for (fs::path candidate = root; !candidate.empty();
       candidate = candidate.parent_path()) {
    if (looks_like_xplane_root(candidate)) {
      return candidate.string();
    }
    if (candidate == candidate.parent_path()) {
      break;
    }
  }
  return root.string();
}

std::vector<std::string> NavdataPaths::apt_candidates() const {
  std::vector<std::string> out;
  std::error_code ec;
  const fs::path root = fs::path(resolved_root());
  const fs::path custom_root = root / "Custom Scenery";
  if (fs::is_directory(custom_root, ec)) {
    for (const auto &entry : fs::directory_iterator(custom_root, ec)) {
      const fs::path apt =
          entry.path() / "Earth nav data" / "apt.dat";
      if (fs::is_regular_file(apt, ec)) {
        out.push_back(apt.string());
      }
    }
    std::sort(out.begin(), out.end()); // deterministic order
  }
  const fs::path global = root / "Global Scenery" / "Global Airports" /
                          "Earth nav data" / "apt.dat";
  if (fs::is_regular_file(global, ec)) {
    out.push_back(global.string());
  }
  return out;
}

std::vector<std::string> NavdataPaths::apt_sources() const {
  const auto candidates = apt_candidates();
  std::vector<std::string> out;
  out.reserve(candidates.size());
  const std::string global_marker =
      (fs::path("Global Scenery") / "Global Airports").string();
  for (const auto &path : candidates) {
    out.push_back(path.find(global_marker) != std::string::npos ? "global"
                                                                : "custom");
  }
  return out;
}

std::string NavdataPaths::cifp_path(std::string_view icao) const {
  return (fs::path(resolved_root()) / "Custom Data" / "CIFP" /
          (to_upper(icao) + ".dat"))
      .string();
}

std::string NavdataPaths::airspace_path() const {
  return (fs::path(resolved_root()) / "Custom Data" / "airspaces" /
          "airspace.txt")
      .string();
}

std::string NavdataPaths::atc_path() const {
  return (fs::path(resolved_root()) / "Custom Data" / "1200 atc data" /
          "Earth nav data" / "atc.dat")
      .string();
}

std::string NavdataPaths::vrp_path() const {
  return (fs::path(resolved_root()) / "Custom Data" / "vrps.csv").string();
}

std::string NavdataPaths::metar_dir() const {
  return (fs::path(resolved_root()) / "Output" / "real weather").string();
}

namespace {
std::string fix_file_name(std::string_view file) {
  return file == "nav" ? "earth_nav.dat" : "earth_fix.dat";
}
} // namespace

std::vector<std::string> NavdataPaths::fix_candidates(
    std::string_view file) const {
  std::vector<std::string> out;
  std::error_code ec;
  const fs::path root = fs::path(resolved_root());
  const std::string name = fix_file_name(file);
  const fs::path custom = root / "Custom Data" / name;
  if (fs::is_regular_file(custom, ec)) {
    out.push_back(custom.string());
  }
  const fs::path global = root / "Resources" / "default data" / name;
  if (fs::is_regular_file(global, ec)) {
    out.push_back(global.string());
  }
  return out;
}

std::vector<std::string> NavdataPaths::fix_sources(
    std::string_view file) const {
  const auto candidates = fix_candidates(file);
  std::vector<std::string> out;
  out.reserve(candidates.size());
  const std::string custom_marker = (fs::path("Custom Data")).string();
  for (const auto &path : candidates) {
    out.push_back(path.find(custom_marker) != std::string::npos ? "custom"
                                                                : "global");
  }
  return out;
}

NavdataPublisher::NavdataPublisher(NavdataPaths paths, Sender sender,
                                   DiagnosticSink diagnostics)
    : paths_(std::move(paths)), sender_(std::move(sender)),
      diagnostics_(std::move(diagnostics)) {}

void NavdataPublisher::publish_for_airport(const std::string &icao) {
  constexpr double kRegionalAptRadiusNm = 100.0;
  const std::string wanted = to_upper(icao);
  if (wanted.empty()) {
    return;
  }
  if (diagnostics_) {
    diagnostics_("navdata root input=" + paths_.xplane_root +
                 " resolved=" + paths_.resolved_root());
  }

  // apt.dat: custom scenery overrides first, then global; each candidate that
  // contains the airport gets its own hash frame (the console keeps the
  // highest-priority source).
  const auto candidates = paths_.apt_candidates();
  const auto sources = paths_.apt_sources();
  std::size_t apt_hits = 0;
  std::optional<std::pair<double, double>> reference;
  if (diagnostics_) {
    diagnostics_("navdata apt candidates=" + std::to_string(candidates.size()) +
                 " for " + wanted);
  }
  for (std::size_t i = 0; i < candidates.size(); ++i) {
    std::ifstream apt(candidates[i]);
    if (!apt.is_open()) {
      if (diagnostics_) {
        diagnostics_("navdata apt open failed: " + candidates[i]);
      }
      continue;
    }
    std::string scoped =
        extract_regional_airport_blocks(apt, {wanted}, kRegionalAptRadiusNm);
    if (scoped.empty()) {
      continue;
    }
    ++apt_hits;
    if (!reference.has_value()) {
      // The airport's reference point scopes the global fix files below.
      std::istringstream scoped_in(scoped);
      reference = find_airport_reference(scoped_in, wanted);
    }
    offer(NavdataKind::Apt, wanted, sources[i], std::move(scoped));
  }

  bool has_cifp = false;
  bool has_airspace = false;
  bool has_atc = false;
  bool has_vrp = false;
  if (auto cifp = read_file_text(paths_.cifp_path(wanted));
      cifp.has_value() && !cifp->empty()) {
    has_cifp = true;
    offer(NavdataKind::Cifp, wanted, "global", std::move(*cifp));
  }
  if (auto airspace = read_file_text(paths_.airspace_path());
      airspace.has_value() && !airspace->empty()) {
    has_airspace = true;
    offer(NavdataKind::Airspace, "", "global", std::move(*airspace));
  }
  if (auto atc = read_file_text(paths_.atc_path());
      atc.has_value() && !atc->empty()) {
    has_atc = true;
    offer(NavdataKind::Atc, "", "global", std::move(*atc));
  }
  if (auto vrp = read_file_text(paths_.vrp_path());
      vrp.has_value() && !vrp->empty()) {
    has_vrp = true;
    offer(NavdataKind::Vrp, "", "global", std::move(*vrp));
  }
  if (reference.has_value()) {
    publish_fixes(reference->first, reference->second);
  } else if (diagnostics_) {
    diagnostics_("navdata fixes skipped: no reference point for " + wanted);
  }
  if (diagnostics_) {
    diagnostics_("navdata scan summary for " + wanted +
                 ": apt_hits=" + std::to_string(apt_hits) +
                 " cifp=" + (has_cifp ? "yes" : "no") +
                 " airspace=" + (has_airspace ? "yes" : "no") +
                 " atc=" + (has_atc ? "yes" : "no") +
                 " vrp=" + (has_vrp ? "yes" : "no"));
  }
}

// publish_fixes ships the bounding-box subset of the global fix files around
// the session airport (phase7 M1): the highest-priority existing copy of each
// file (Custom Data AIRAC over default data), filtered to kFixBoxRadiusNm.
// earth_fix.dat rows start "lat lon ident…" (lat field 0); earth_nav.dat rows
// start "type lat lon…" (lat field 1). Missing files are silently skipped.
void NavdataPublisher::publish_fixes(double lat, double lon) {
  constexpr double kFixBoxRadiusNm = 150.0;
  const struct {
    const char *file;
    std::size_t lat_field;
  } files[] = {{"fix", 0}, {"nav", 1}};
  for (const auto &spec : files) {
    const auto candidates = paths_.fix_candidates(spec.file);
    const auto sources = paths_.fix_sources(spec.file);
    for (std::size_t i = 0; i < candidates.size(); ++i) {
      std::ifstream in(candidates[i]);
      if (!in.is_open()) {
        continue;
      }
      std::string scoped =
          extract_rows_in_box(in, spec.lat_field, lat, lon, kFixBoxRadiusNm);
      if (scoped.empty()) {
        continue;
      }
      if (diagnostics_) {
        diagnostics_(std::string("navdata fixes ") + spec.file + " from " +
                     candidates[i]);
      }
      offer_fixes(spec.file, sources[i], std::move(scoped));
      break; // highest-priority existing copy wins
    }
  }
}

bool NavdataPublisher::handle_request(const NavdataRequest &request) {
  const auto entry = by_hash_.find(request.md5);
  if (entry == by_hash_.end()) {
    return false;
  }
  if (entry->second.kind == NavdataKind::Fixes) {
    sender_(build_fixes_frame(entry->second.file, request.md5,
                              entry->second.source, entry->second.text));
    return true;
  }
  sender_(build_navdata_frame(entry->second.kind, entry->second.icao,
                              request.md5, entry->second.source,
                              entry->second.text));
  return true;
}

void NavdataPublisher::offer(NavdataKind kind, const std::string &icao,
                             const std::string &source, std::string text) {
  const std::string hash = md5_hex(text);
  sender_(build_navdata_hash_frame(kind, icao, hash, source));
  by_hash_[hash] = Entry{kind, icao, source, std::move(text), ""};
}

void NavdataPublisher::offer_fixes(const std::string &file,
                                   const std::string &source,
                                   std::string text) {
  const std::string hash = md5_hex(text);
  sender_(build_fixes_hash_frame(file, hash, source));
  by_hash_[hash] = Entry{NavdataKind::Fixes, "", source, std::move(text), file};
}

std::optional<std::string> read_file_text(const std::string &path) {
  std::ifstream file(path, std::ios::binary);
  if (!file.is_open()) {
    return std::nullopt;
  }
  std::ostringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

std::optional<std::string> newest_metar_file(const std::string &dir) {
  std::error_code ec;
  if (!fs::is_directory(dir, ec)) {
    return std::nullopt;
  }
  std::optional<std::string> newest;
  fs::file_time_type newest_time{};
  for (const auto &entry : fs::directory_iterator(dir, ec)) {
    if (!entry.is_regular_file(ec)) {
      continue;
    }
    const std::string name = entry.path().filename().string();
    if (name.rfind("metar-", 0) != 0 ||
        name.find(".txt") == std::string::npos) {
      continue;
    }
    const auto mtime = entry.last_write_time(ec);
    if (!newest.has_value() || mtime > newest_time) {
      newest = entry.path().string();
      newest_time = mtime;
    }
  }
  return newest;
}

std::string build_metar_frame(std::string_view text) {
  std::ostringstream out;
  out << "{\"type\":\"weather_metar\",\"text\":\"" << json_escape(text)
      << "\"}";
  return out.str();
}

} // namespace zoal_atc::navdata
