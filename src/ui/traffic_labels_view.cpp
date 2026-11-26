#include "traffic_labels_view.hpp"

#include "hud_palette.hpp"

#include <algorithm>
#include <string>

namespace zoal_atc::ui {

namespace {

using gui::LabelDetail;
using gui::TrafficLabel;
using hud_style::Palette;

constexpr float kPadX = 9.0f;
constexpr float kPadY = 5.0f;
constexpr float kLine = 17.0f;
constexpr float kLeader = 12.0f;
constexpr float kDot = 3.5f;
constexpr float kGapX = 7.0f;

// A run of text on one line: what is said, in which face and colour.
struct Run {
  std::string text;
  ImFont *font;
  float px;
  ImU32 colour;
  float width = 0.0f;
};

float runs_width(std::vector<Run> &runs) {
  float width = 0.0f;
  for (Run &run : runs) {
    run.width = measure(run.font, run.px, run.text).x;
    width += run.width;
  }
  if (!runs.empty()) {
    width += kGapX * static_cast<float>(runs.size() - 1);
  }
  return width;
}

void draw_runs(ImDrawList *draw, const std::vector<Run> &runs, float x, float top) {
  for (const Run &run : runs) {
    draw->AddText(run.font, run.px, ImVec2(x, top + (kLine - run.px) * 0.5f), run.colour,
                  run.text.c_str());
    x += run.width + kGapX;
  }
}

ImU32 trend_colour(const Palette &p, int trend, float opacity) {
  const unsigned hex = trend > 0 ? p.active : (trend < 0 ? p.warn : p.text_dim);
  return rgba(hex, opacity);
}

void draw_label(ImDrawList *draw, const Typeface &face, const Palette &p,
                const PlacedLabel &placed) {
  const TrafficLabel &label = placed.label;
  const float o = label.opacity;
  const float name_px = face.css(label.detail == LabelDetail::callsign ? 12.0f : 13.0f);
  const float small_px = face.css(11.5f);

  // The first line: who it is.
  std::vector<Run> first{{label.callsign, face.semibold, name_px, rgba(p.text, o)}};
  if (label.detail == LabelDetail::full && !label.type.empty()) {
    first.push_back({label.type, face.regular, small_px, rgba(p.text_dim, o)});
  }
  if (label.detail == LabelDetail::compact) {
    first.push_back({label.altitude, face.regular, small_px, rgba(p.text_feed, o)});
  }
  // The second line, near ones only: what it is doing and how far it is.
  std::vector<Run> second;
  if (label.detail == LabelDetail::full) {
    second.push_back({label.altitude, face.semibold, small_px, rgba(p.text_feed, o)});
    if (!label.vertical.empty()) {
      second.push_back({label.vertical, face.regular, small_px, trend_colour(p, label.trend, o)});
    }
    second.push_back({label.speed, face.regular, small_px, rgba(p.text_dim, o)});
    if (!label.distance.empty()) {
      second.push_back({label.distance, face.regular, small_px, rgba(p.text_dim, o)});
    }
  }

  const float dot_w = kDot * 2.0f + 6.0f;
  const float width =
      kPadX * 2.0f + std::max(dot_w + runs_width(first), second.empty() ? 0.0f : runs_width(second));
  const float height = kPadY * 2.0f + kLine * (second.empty() ? 1.0f : 2.0f);
  const ImVec2 anchor = placed.anchor;
  const ImVec2 min(anchor.x - width * 0.5f, anchor.y - kLeader - height);
  const ImVec2 max(min.x + width, min.y + height);
  const float radius = second.empty() ? height * 0.5f : 9.0f;

  // The leader and the point it marks.
  const ImU32 accent = rgba(p.accent, o);
  draw->AddLine(ImVec2(anchor.x, max.y), anchor, faded(accent, 0.55f), 1.0f);
  draw->AddCircleFilled(anchor, 2.0f, faded(accent, 0.8f));

  // The glass: the HUD's, edged in the accent; a tracked tag wears it solid.
  draw->AddRectFilled(min, max, faded(hud_style::glass(p), std::max(o, 0.6f)), radius);
  if (label.followed) {
    draw->AddRect(min, max, accent, radius, 2.0f);
  } else if (label.tracked) {
    draw->AddRect(min, max, faded(accent, 0.85f), radius, 1.5f);
  } else {
    draw->AddRect(min, max, faded(hud_style::edge(p), o), radius, 1.0f);
  }

  // A person or the console's AI, as the panel's Traffic tab marks them.
  const float top = min.y + kPadY;
  draw->AddCircleFilled(ImVec2(min.x + kPadX + kDot, top + kLine * 0.5f), kDot,
                        rgba(label.pilot ? p.active : p.accent, o));
  draw_runs(draw, first, min.x + kPadX + dot_w, top);
  if (!second.empty()) {
    draw_runs(draw, second, min.x + kPadX, top + kLine);
  }
}

} // namespace

void draw_traffic_labels(std::vector<PlacedLabel> labels, gui::HudTheme theme,
                         const Typeface &face) {
  const ImGuiIO &io = ImGui::GetIO();
  ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
  ImGui::SetNextWindowSize(io.DisplaySize);
  ImGui::Begin("zoal-atc traffic labels", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
                   ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
  ImDrawList *draw = ImGui::GetWindowDrawList();
  const Palette &p = hud_style::palette(theme);
  std::stable_sort(labels.begin(), labels.end(),
                   [](const PlacedLabel &a, const PlacedLabel &b) {
                     // Far first; the tracked one last of all, on top.
                     if (a.label.tracked != b.label.tracked) {
                       return !a.label.tracked;
                     }
                     return a.depth > b.depth;
                   });
  for (const PlacedLabel &placed : labels) {
    draw_label(draw, face, p, placed);
  }
  ImGui::End();
}

} // namespace zoal_atc::ui
