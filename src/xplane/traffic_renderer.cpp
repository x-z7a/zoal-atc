#include "traffic_renderer.hpp"

#include "zoal_atc/traffic/clock_sync.hpp"

#include "XPMPAircraft.h"
#include "XPMPMultiplayer.h"

#include <cstring>

namespace zoal_atc::xplane {

namespace {

void copy_text(char *dst, std::size_t size, const std::string &src) {
  std::strncpy(dst, src.c_str(), size - 1);
  dst[size - 1] = '\0';
}

} // namespace

// One console track drawn by XPMP2. It holds no state of its own beyond what
// XPMP2 needs: where it is comes from the registry every frame.
class ConsoleAircraft : public XPMP2::Aircraft {
public:
  ConsoleAircraft(const traffic::Identity &who,
                  const traffic::TrafficRegistry &registry)
      : XPMP2::Aircraft(who.icao_type, who.airline, who.livery, who.mode_s, "",
                        who.callsign),
        id_(who.id), registry_(registry) {
    relabel(who);
    // Participants first for TCAS slots: a real pilot outranks an AI one.
    aiPrio = who.kind == traffic::TrafficKind::Participant ? 0 : 1;
    SetVisible(false); // until the first state is known
  }

  void relabel(const traffic::Identity &who) {
    label = who.callsign.empty() ? who.id : who.callsign;
    copy_text(acInfoTexts.icaoAcType, sizeof(acInfoTexts.icaoAcType), who.icao_type);
    copy_text(acInfoTexts.icaoAirline, sizeof(acInfoTexts.icaoAirline), who.airline);
    copy_text(acInfoTexts.flightNum, sizeof(acInfoTexts.flightNum), who.callsign);
    pilot_ = who.kind == traffic::TrafficKind::Participant;
    colour_label();
  }

  void set_tracked(bool tracked) {
    tracked_ = tracked;
    colour_label();
  }

  void UpdatePosition(float, int) override {
    auto s = registry_.state(id_, traffic::monotonic_seconds());
    if (!s) {
      SetVisible(false);
      return;
    }
    if (!IsVisible()) {
      SetVisible(true);
    }
    bClampToGround = s->on_ground;
    SetLocation(s->lat_deg, s->lon_deg, s->alt_ft_msl, s->on_ground);
    SetHeading(static_cast<float>(s->heading_deg));
    SetPitch(static_cast<float>(s->pitch_deg));
    SetRoll(static_cast<float>(s->roll_deg));
    SetGearRatio(static_cast<float>(s->gear));
    SetFlapRatio(static_cast<float>(s->flaps));
    SetSpeedbrakeRatio(static_cast<float>(s->speedbrake));
    SetThrustRatio(static_cast<float>(s->thrust));
    SetLightsNav(s->lights & traffic::kLightNav);
    SetLightsBeacon(s->lights & traffic::kLightBeacon);
    SetLightsStrobe(s->lights & traffic::kLightStrobe);
    SetLightsTaxi(s->lights & traffic::kLightTaxi);
    SetLightsLanding(s->lights & traffic::kLightLanding);
    // Rough engine/prop animation from thrust; a real value can come later.
    SetEngineRotRpm(static_cast<float>(600.0 + 2000.0 * s->thrust));
    SetPropRotRpm(static_cast<float>(600.0 + 1800.0 * s->thrust));
    SetTireRotRpm(static_cast<float>(s->on_ground ? s->groundspeed_kts * 30.0 : 0.0));
  }

private:
  // Cyan while the pilot tracks it from the panel's Traffic tab, green for a
  // person, XPMP2's yellow for the console's AI.
  void colour_label() {
    if (tracked_) {
      colLabel[0] = 0.3f; colLabel[1] = 0.85f; colLabel[2] = 1.0f;
    } else if (pilot_) {
      colLabel[0] = 0.4f; colLabel[1] = 1.0f; colLabel[2] = 0.4f;
    } else {
      colLabel[0] = 1.0f; colLabel[1] = 1.0f; colLabel[2] = 0.0f;
    }
  }

  std::string id_;
  bool pilot_ = false;
  bool tracked_ = false;
  const traffic::TrafficRegistry &registry_;
};

TrafficRenderer::TrafficRenderer(const traffic::TrafficRegistry &registry)
    : registry_(registry) {}

TrafficRenderer::~TrafficRenderer() { remove_all(); }

void TrafficRenderer::apply(const traffic::TrafficRegistry::Changes &changes) {
  for (const auto &id : changes.removed) {
    aircraft_.erase(id);
  }
  for (const auto &who : changes.added) {
    auto aircraft = std::make_unique<ConsoleAircraft>(who, registry_);
    aircraft->set_tracked(who.id == tracked_);
    aircraft_[who.id] = std::move(aircraft);
  }
  for (const auto &who : changes.remodelled) {
    if (auto found = aircraft_.find(who.id); found != aircraft_.end()) {
      found->second->ChangeModel(who.icao_type, who.airline, who.livery);
      found->second->relabel(who);
    }
  }
  for (const auto &who : changes.relabelled) {
    if (auto found = aircraft_.find(who.id); found != aircraft_.end()) {
      found->second->relabel(who);
    }
  }
}

void TrafficRenderer::remove_all() { aircraft_.clear(); }

void TrafficRenderer::set_tracked(const std::string &id) {
  if (id == tracked_) {
    return;
  }
  if (auto found = aircraft_.find(tracked_); found != aircraft_.end()) {
    found->second->set_tracked(false);
  }
  tracked_ = id;
  if (auto found = aircraft_.find(tracked_); found != aircraft_.end()) {
    found->second->set_tracked(true);
  }
}

std::set<std::uint32_t> TrafficRenderer::mode_s_ids() const {
  std::set<std::uint32_t> ids;
  for (const auto &[id, aircraft] : aircraft_) {
    ids.insert(static_cast<std::uint32_t>(aircraft->GetModeS_ID()));
  }
  return ids;
}

} // namespace zoal_atc::xplane
