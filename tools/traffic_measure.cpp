// traffic_measure: how what a cockpit draws of other aircraft depends on how
// often it is sent them (x-z7a/atc#1128, #1129).
//
// It reads truth CSVs the console writes (`make measure-traffic` in the
// console repo): ai-*.csv is an AI aircraft's motion at a fine step, real-*.csv
// a real pilot's telemetry at its full rate with two clocks, the gateway's
// arrival stamp and the simulator's own. Each truth is fed through the real
// TrackSmoother under several feeds, rendered at 60 Hz the way the registry
// renders it, and compared per phase with a reference:
//
//   - AI: samples every I seconds, each 2 s ahead of the clock (the fleet's
//     lead), drawn at render delay 0. Reference: the truth itself.
//   - Real: the native rate stamped on arrival (today), capped at 2 Hz and at
//     1 Hz, and the same stamped with the simulator's clock mapped onto the
//     console's (a sliding-window minimum offset: the fix #1129 asks about). Drawn at the 1.5 s
//     participant delay. Reference: the native feed on the simulator's clock,
//     which is as near the truth as the recording gets.
//
// Reported: horizontal position error (p95, max, metres), altitude error
// (max, feet), heading and roll error (max, degrees), and the acceleration
// the drawing shows from one frame to the next (p99 and max, m/s^2) beside
// the reference's largest. An airliner's real accelerations are a few m/s^2;
// anything far above that is a judder the eye sees. And how sharply the drawn
// heading, pitch and roll rates change from one frame to the next (max,
// deg/s): an angle blended in a straight line between reports changes its
// rate all at once at each of them, which is a step the eye sees.
//
// Usage: traffic_measure <truth-dir>
#include "zoal_atc/traffic/track_smoother.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace {
using zoal_atc::traffic::Sample;
using zoal_atc::traffic::TrackSmoother;

constexpr double kPi = 3.14159265358979323846;
constexpr double kFrame = 1.0 / 60.0;
constexpr double kLead = 2.0;
constexpr double kParticipantDelay = 1.5;

struct Row {
  Sample s;
  std::string phase;
  double sim_t = 0.0;
};

std::vector<Row> read_truth(const std::string &path) {
  std::ifstream in(path);
  std::string line;
  std::getline(in, line); // header
  std::vector<Row> rows;
  while (std::getline(in, line)) {
    std::stringstream ss(line);
    std::vector<std::string> f;
    std::string cell;
    while (std::getline(ss, cell, ',')) {
      f.push_back(cell);
    }
    if (f.size() < 16) {
      continue;
    }
    Row r;
    auto d = [&](std::size_t i) { return std::stod(f[i]); };
    r.s.t_s = d(0);
    r.s.lat_deg = d(1);
    r.s.lon_deg = d(2);
    r.s.alt_ft_msl = d(3);
    r.s.heading_deg = d(4);
    r.s.pitch_deg = d(5);
    r.s.roll_deg = d(6);
    r.s.groundspeed_kts = d(7);
    r.s.vertical_speed_fpm = d(8);
    r.s.track_deg = d(9);
    r.s.on_ground = f[10] == "1";
    r.s.gear = d(11);
    r.s.flaps = d(12);
    r.s.speedbrake = d(13);
    r.s.thrust = d(14);
    r.phase = f[15];
    r.sim_t = f.size() > 16 ? d(16) : r.s.t_s;
    rows.push_back(r);
  }
  return rows;
}

// Coarse phase for the table: the AI flight's phases grouped as the real
// pilots' are, ground / low / air.
std::string group(const std::string &phase, const Sample &s, double field_ft) {
  if (phase == "ground" || phase == "low" || phase == "air") {
    return phase;
  }
  if (s.on_ground) {
    return "ground";
  }
  return s.alt_ft_msl - field_ft < 1500.0 ? "low" : "air";
}

// The signed turn from b to a, in (-180, 180].
double angle_err_signed(double a, double b) { return std::fmod(a - b + 540.0, 360.0) - 180.0; }

double angle_err(double a, double b) {
  return std::abs(std::fmod(a - b + 540.0, 360.0) - 180.0);
}

struct Point {
  double x = 0, y = 0; // metres east/north of an origin
};

Point local(const Sample &s, double lat0, double lon0) {
  const double m_lon = 111320.0 * std::cos(lat0 * kPi / 180.0);
  return {(s.lon_deg - lon0) * m_lon, (s.lat_deg - lat0) * 111132.0};
}

struct Stats {
  std::vector<double> pos, accel, hdg_kink, pitch_kink, roll_kink;
  double alt = 0, hdg = 0, roll = 0, ref_accel = 0;
  void add(double p, double a, double drawn_accel, double reference_accel, double h, double r, bool has_accel) {
    pos.push_back(p);
    if (has_accel) {
      accel.push_back(drawn_accel);
      ref_accel = std::max(ref_accel, reference_accel);
    }
    alt = std::max(alt, a);
    hdg = std::max(hdg, h);
    roll = std::max(roll, r);
  }
  static double pct(std::vector<double> v, double q) {
    if (v.empty()) {
      return 0.0;
    }
    std::sort(v.begin(), v.end());
    return v[std::min(v.size() - 1, static_cast<std::size_t>(q * static_cast<double>(v.size())))];
  }
};

using Feed = std::vector<std::pair<double, Sample>>; // (delivered at, sample)
using Reference = std::function<std::optional<Sample>(double)>;

// Renders a feed at 60 Hz the way the registry does and compares it with
// the reference, per phase.
std::map<std::string, Stats> render(const Feed &feed, double delay, double from, double to,
                                    const Reference &reference, const std::vector<Row> &truth,
                                    double field_ft) {
  TrackSmoother::Config config;
  TrackSmoother smoother(config);
  std::map<std::string, Stats> out;
  std::size_t next = 0;
  std::size_t phase_at = 0;
  const double lat0 = truth.front().s.lat_deg, lon0 = truth.front().s.lon_deg;
  Point drawn[3], ref[3];
  double hdg[3] = {}, pitch[3] = {}, roll[3] = {};
  int have = 0;
  for (double t = from; t < to; t += kFrame) {
    while (next < feed.size() && feed[next].first <= t) {
      smoother.add(feed[next].second);
      ++next;
    }
    const double at = t - delay;
    smoother.prune_before(at);
    auto d = smoother.evaluate(at);
    auto r = reference(at);
    if (!d || !r) {
      have = 0;
      continue;
    }
    while (phase_at + 1 < truth.size() && truth[phase_at + 1].s.t_s <= at) {
      ++phase_at;
    }
    drawn[0] = drawn[1];
    drawn[1] = drawn[2];
    drawn[2] = local(*d, lat0, lon0);
    ref[0] = ref[1];
    ref[1] = ref[2];
    ref[2] = local(*r, lat0, lon0);
    for (double *v : {hdg, pitch, roll}) {
      v[0] = v[1];
      v[1] = v[2];
    }
    // Heading unwrapped onto the previous frame's, so north is no step.
    hdg[2] = hdg[1] + angle_err_signed(d->heading_deg, hdg[1]);
    pitch[2] = d->pitch_deg;
    roll[2] = d->roll_deg;
    have = std::min(have + 1, 3);
    double drawn_accel = 0.0, reference_accel = 0.0;
    if (have == 3) {
      auto accel = [](const Point *p) {
        return std::hypot((p[2].x - 2 * p[1].x + p[0].x) / (kFrame * kFrame),
                          (p[2].y - 2 * p[1].y + p[0].y) / (kFrame * kFrame));
      };
      drawn_accel = accel(drawn);
      reference_accel = accel(ref);
    }
    // The change in an angle's rate from one frame to the next, deg/s.
    auto kink = [](const double *v) { return std::abs(v[2] - 2 * v[1] + v[0]) / kFrame; };
    const Point dp = local(*d, lat0, lon0), rp = local(*r, lat0, lon0);
    const std::string phase = group(truth[phase_at].phase, truth[phase_at].s, field_ft);
    out[phase].add(std::hypot(dp.x - rp.x, dp.y - rp.y), std::abs(d->alt_ft_msl - r->alt_ft_msl), drawn_accel,
                   reference_accel,
                   angle_err(d->heading_deg, r->heading_deg), std::abs(d->roll_deg - r->roll_deg), have == 3);
    if (have == 3) {
      out[phase].hdg_kink.push_back(kink(hdg));
      out[phase].pitch_kink.push_back(kink(pitch));
      out[phase].roll_kink.push_back(kink(roll));
    }
  }
  return out;
}

// The truth itself, interpolated linearly between its fine rows.
Reference truth_reference(const std::vector<Row> &truth) {
  return [&truth](double at) -> std::optional<Sample> {
    if (at < truth.front().s.t_s || at > truth.back().s.t_s) {
      return std::nullopt;
    }
    auto it = std::upper_bound(truth.begin(), truth.end(), at,
                               [](double t, const Row &r) { return t < r.s.t_s; });
    if (it == truth.begin() || it == truth.end()) {
      return truth.back().s;
    }
    const Row &b = *it, &a = *std::prev(it);
    Sample s = zoal_atc::traffic::interpolate(a.s, b.s, at);
    return s;
  };
}

void print(const std::string &file, const std::string &feed, const std::map<std::string, Stats> &stats) {
  for (const auto &[phase, s] : stats) {
    std::printf("| %s | %s | %s | %.1f | %.1f | %.1f | %.1f | %.1f | %.1f | %.1f | %.1f | %.2f | %.2f | %.2f |\n",
                file.c_str(), feed.c_str(), phase.c_str(), Stats::pct(s.pos, 0.95), Stats::pct(s.pos, 1.0), s.alt,
                s.hdg, s.roll, Stats::pct(s.accel, 0.99), Stats::pct(s.accel, 1.0), s.ref_accel,
                Stats::pct(s.hdg_kink, 1.0), Stats::pct(s.pitch_kink, 1.0), Stats::pct(s.roll_kink, 1.0));
  }
}

void measure_ai(const std::string &name, const std::vector<Row> &truth) {
  const double field = truth.front().s.alt_ft_msl;
  const double end = truth.back().s.t_s;
  for (double interval : {0.25, 0.5, 1.0, 2.0}) {
    // A frame at wall time k*I carries the sample Lead ahead of it.
    Feed feed;
    for (std::size_t k = 0;; ++k) {
      const double wall = static_cast<double>(k) * interval;
      const double sample_t = wall + kLead;
      if (sample_t > end) {
        break;
      }
      auto s = truth_reference(truth)(sample_t);
      if (s) {
        feed.emplace_back(wall, *s);
      }
    }
    char label[32];
    std::snprintf(label, sizeof label, "every %.2f s", interval);
    print(name, label, render(feed, 0.0, kLead, end - kLead, truth_reference(truth), truth, field));
  }
}

void measure_real(const std::string &name, const std::vector<Row> &truth) {
  // The simulator's clock mapped onto the console's: the smallest
  // arrival-minus-sample offset over the last kClockWindow seconds, slewed. The
  // simulator's clock is not the wall's: over a recorded flight it drifted
  // 18 s, at up to 7 ms a second either way, which ClockSync's one-way creep
  // of 2 ms a second cannot follow.
  // The offset used moves toward that minimum at no more than kClockSlew
  // seconds a second: a minimum that steps would step every stamp after it,
  // which the smoother draws as a jump in speed.
  constexpr double kClockWindow = 10.0;
  constexpr double kClockSlew = 0.01;
  std::vector<double> sim_on_console(truth.size());
  {
    std::deque<std::pair<double, double>> window; // (arrival, offset), offsets increasing
    double used = 0.0, last_sim = 0.0;
    for (std::size_t i = 0; i < truth.size(); ++i) {
      const double arrived = truth[i].s.t_s, offset = arrived - truth[i].sim_t;
      while (!window.empty() && window.back().second >= offset) {
        window.pop_back();
      }
      window.emplace_back(arrived, offset);
      while (window.front().first < arrived - kClockWindow) {
        window.pop_front();
      }
      if (i == 0) {
        used = window.front().second;
      } else {
        const double step = kClockSlew * std::max(0.0, truth[i].sim_t - last_sim);
        used += std::clamp(window.front().second - used, -step, step);
      }
      last_sim = truth[i].sim_t;
      sim_on_console[i] = truth[i].sim_t + used;
    }
  }
  auto feed_of = [&](double cap, bool sim_clock) {
    Feed feed;
    double last = -1e9;
    for (std::size_t i = 0; i < truth.size(); ++i) {
      const double arrived = truth[i].s.t_s;
      if (arrived - last < cap) {
        continue;
      }
      last = arrived;
      Sample s = truth[i].s;
      s.t_s = sim_clock ? sim_on_console[i] : arrived;
      feed.emplace_back(arrived, s);
    }
    return feed;
  };
  // Reference: every report, on the simulator's clock.
  TrackSmoother::Config config;
  config.max_samples = 1u << 20;
  auto reference_smoother = std::make_shared<TrackSmoother>(config);
  for (std::size_t i = 0; i < truth.size(); ++i) {
    Sample s = truth[i].s;
    s.t_s = sim_on_console[i];
    reference_smoother->add(s);
  }
  Reference reference = [reference_smoother](double at) { return reference_smoother->evaluate(at); };
  const double field = 0.0;
  const double from = truth.front().s.t_s + 5.0, to = truth.back().s.t_s;
  struct Variant {
    const char *label;
    double cap;
    bool sim;
  };
  for (const Variant &v : {Variant{"native, arrival stamp (today)", 0.0, false},
                           Variant{"2 Hz cap, arrival stamp", 0.45, false},
                           Variant{"1 Hz cap, arrival stamp", 0.95, false},
                           Variant{"native, sim clock", 0.0, true},
                           Variant{"2 Hz cap, sim clock", 0.45, true},
                           Variant{"1 Hz cap, sim clock", 0.95, true}}) {
    print(name, v.label, render(feed_of(v.cap, v.sim), kParticipantDelay, from, to, reference, truth, field));
  }
}

} // namespace

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "usage: traffic_measure <truth-dir>\n";
    return 2;
  }
  std::vector<std::filesystem::path> files;
  for (const auto &entry : std::filesystem::directory_iterator(argv[1])) {
    if (entry.path().extension() == ".csv") {
      files.push_back(entry.path());
    }
  }
  std::sort(files.begin(), files.end());
  std::printf("| truth | feed | phase | pos p95 m | pos max m | alt max ft | hdg max ° | roll max ° | drawn accel p99 m/s² | drawn accel max m/s² | reference accel max m/s² | heading rate step max °/s | pitch rate step max °/s | roll rate step max °/s |\n");
  std::printf("|---|---|---|---|---|---|---|---|---|---|---|---|---|---|\n");
  for (const auto &path : files) {
    const auto truth = read_truth(path.string());
    if (truth.size() < 10) {
      continue;
    }
    const std::string name = path.stem().string();
    if (name.rfind("ai-", 0) == 0) {
      measure_ai(name, truth);
    } else {
      measure_real(name, truth);
    }
  }
  return 0;
}
