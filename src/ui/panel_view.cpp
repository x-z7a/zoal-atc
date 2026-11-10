#include "panel_view.hpp"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <string>
#include <utility>

#include "imgui_internal.h"

namespace zoal_atc::ui {

namespace {

using gui::HudEntry;
using gui::HudTheme;
using gui::HudTone;
using gui::PanelSession;

constexpr float kPi = 3.14159265358979f;

// --- the palette: tokens.css, per theme ------------------------------------------

struct Palette {
  ImU32 glass, surface, raised, surface_comm, glass_card, glass_well;
  ImU32 border, border_strong;
  ImU32 text, text_dim, text_feed;
  ImU32 accent, on_accent, accent_container, on_accent_container;
  ImU32 tonal, on_tonal, outline;
  ImU32 active, warn, on_warn, warn_surface, danger;
  ImU32 pilot_bubble, pilot_border;
  ImU32 delivery, seat, tower;
  ImU32 shadow;
  // The swatch a theme card shows for itself, whatever theme is worn.
  unsigned swatch_bg, swatch_text, swatch_accent, swatch_active;
};

Palette graphite() {
  Palette p{};
  p.glass = rgba(0x19191b, 0.90f);
  p.surface = rgba(0x222224);
  p.raised = rgba(0x2d2d30);
  p.surface_comm = rgba(0xfff8f0, 0.07f);
  p.glass_card = rgba(0xfff8f0, 0.045f);
  p.glass_well = rgba(0x000000, 0.22f);
  p.border = rgba(0xfff8f0, 0.08f);
  p.border_strong = rgba(0xfff8f0, 0.15f);
  p.text = rgba(0xefebe5);
  p.text_dim = rgba(0xa6a099);
  p.text_feed = rgba(0xd8d2ca);
  p.accent = rgba(0xefb45c);
  p.on_accent = rgba(0x2e1f06);
  p.accent_container = rgba(0x3d3121);
  p.on_accent_container = rgba(0xf8dcb2);
  p.tonal = rgba(0x2c3529);
  p.on_tonal = rgba(0xd3ecca);
  p.outline = rgba(0x4a4845);
  p.active = rgba(0xa9d39f);
  p.warn = rgba(0xff9b6a);
  p.on_warn = rgba(0x2b1206);
  p.warn_surface = rgba(0x3a2419);
  p.danger = rgba(0xff8a7a);
  p.pilot_bubble = rgba(0xa9d39f, 0.13f);
  p.pilot_border = rgba(0xa9d39f, 0.26f);
  p.delivery = rgba(0xc4b3e6);
  p.seat = rgba(0xe8a989);
  p.tower = rgba(0x9fc4e8);
  p.shadow = rgba(0x000000, 0.45f);
  return p;
}

Palette daylight() {
  Palette p{};
  p.glass = rgba(0xf2eee7, 0.92f);
  p.surface = rgba(0xfbf8f3);
  p.raised = rgba(0xe8e2d8);
  p.surface_comm = rgba(0xffffff, 0.72f);
  p.glass_card = rgba(0xffffff, 0.55f);
  p.glass_well = rgba(0x463219, 0.06f);
  p.border = rgba(0x463219, 0.12f);
  p.border_strong = rgba(0x463219, 0.21f);
  p.text = rgba(0x27231e);
  p.text_dim = rgba(0x6b6358);
  p.text_feed = rgba(0x3d3731);
  p.accent = rgba(0xa35d08);
  p.on_accent = rgba(0xffffff);
  p.accent_container = rgba(0xf2dab4);
  p.on_accent_container = rgba(0x48290a);
  p.tonal = rgba(0xd6e9cf);
  p.on_tonal = rgba(0x12300f);
  p.outline = rgba(0xc6bdb0);
  p.active = rgba(0x3b7738);
  p.warn = rgba(0xc0561d);
  p.on_warn = rgba(0xffffff);
  p.warn_surface = rgba(0xfae2d3);
  p.danger = rgba(0xb93a2a);
  p.pilot_bubble = rgba(0x3b7738, 0.12f);
  p.pilot_border = rgba(0x3b7738, 0.28f);
  p.delivery = rgba(0x6d53a6);
  p.seat = rgba(0xad552a);
  p.tower = rgba(0x2d6a9b);
  p.shadow = rgba(0x463219, 0.22f);
  return p;
}

Palette night() {
  Palette p{};
  p.glass = rgba(0x0e0c0b, 0.92f);
  p.surface = rgba(0x161312);
  p.raised = rgba(0x1f1b18);
  p.surface_comm = rgba(0xffebd7, 0.05f);
  p.glass_card = rgba(0xffebd7, 0.035f);
  p.glass_well = rgba(0x000000, 0.30f);
  p.border = rgba(0xffebd7, 0.07f);
  p.border_strong = rgba(0xffebd7, 0.12f);
  p.text = rgba(0xc8b8a7);
  p.text_dim = rgba(0x85776a);
  p.text_feed = rgba(0xb4a493);
  p.accent = rgba(0xcc8748);
  p.on_accent = rgba(0x1e1004);
  p.accent_container = rgba(0x33241a);
  p.on_accent_container = rgba(0xe4c09d);
  p.tonal = rgba(0x1f271c);
  p.on_tonal = rgba(0xbfd4b6);
  p.outline = rgba(0x3b332d);
  p.active = rgba(0x8cab81);
  p.warn = rgba(0xd8844f);
  p.on_warn = rgba(0x1f0c03);
  p.warn_surface = rgba(0x2a1a10);
  p.danger = rgba(0xd4705f);
  p.pilot_bubble = rgba(0x8cab81, 0.11f);
  p.pilot_border = rgba(0x8cab81, 0.22f);
  p.delivery = rgba(0xa394c0);
  p.seat = rgba(0xc48b70);
  p.tower = rgba(0x8aa5c0);
  p.shadow = rgba(0x000000, 0.55f);
  return p;
}

const Palette &palette(HudTheme theme) {
  static const Palette kGraphite = graphite();
  static const Palette kDaylight = daylight();
  static const Palette kNight = night();
  switch (theme) {
  case HudTheme::daylight:
    return kDaylight;
  case HudTheme::night:
    return kNight;
  case HudTheme::graphite:
    break;
  }
  return kGraphite;
}

struct ThemeOption {
  HudTheme theme;
  const char *name;
  const char *description;
  unsigned bg, text, accent, active;
};

constexpr ThemeOption kThemes[] = {
    {HudTheme::graphite, "Graphite", "Warm dark glass. The default.", 0x19191b,
     0xefebe5, 0xefb45c, 0xa9d39f},
    {HudTheme::daylight, "Daylight", "Light glass for a bright cockpit.",
     0xf2eee7, 0x27231e, 0xa35d08, 0x3b7738},
    {HudTheme::night, "Night", "Dim and low-glare, for flying in the dark.",
     0x0e0c0b, 0xc8b8a7, 0xcc8748, 0x8cab81},
};

ImU32 seat_colour(const Palette &p, HudTone tone) {
  switch (tone) {
  case HudTone::pilot:
    return p.active;
  case HudTone::delivery:
    return p.delivery;
  case HudTone::surface:
    return p.seat;
  case HudTone::tower:
    return p.tower;
  case HudTone::radar:
    return p.accent;
  case HudTone::advisory:
  case HudTone::atc:
    break;
  }
  return p.text_dim;
}

// --- sizes, in boxels: the React panel's CSS px ----------------------------------

constexpr float kPadTop = 12.0f;
constexpr float kPadX = 14.0f;
constexpr float kPadBottom = 14.0f;
constexpr float kGap = 10.0f;
constexpr float kRadius = 10.0f;
constexpr float kRadiusLg = 14.0f;
constexpr float kButtonHeight = 34.0f;
constexpr float kInputHeight = 36.0f;
constexpr float kTileMin = 170.0f;
constexpr float kTileGap = 6.0f;
constexpr float kTileHeight = 80.0f;
constexpr float kLabelTracking = 0.07f;
constexpr float kStickSlack = 48.0f;
constexpr float kWindowRadius = 16.0f;

// One frame's drawing context.
struct Ui {
  const Typeface &face;
  const Palette &p;
  ImDrawList *draw;
  const PanelFrame &frame;

  float css(float px) const { return face.css(px); }

  void later(std::function<void()> action) const {
    if (frame.later != nullptr) {
      frame.later->push_back(std::move(action));
    } else {
      action();
    }
  }
};

float centred(float top, float box, float size) {
  return top + (box - size) * 0.5f;
}

// A path with its own radius at each corner: CSS can tuck one corner of a
// bubble in to 4px, which ImGui's all-or-nothing corner flags cannot.
void rounded_path(ImDrawList *draw, ImVec2 a, ImVec2 b, float tl, float tr,
                  float br, float bl) {
  auto corner = [&](ImVec2 centre, float r, float from, float to,
                    ImVec2 point) {
    if (r <= 0.0f) {
      draw->PathLineTo(point);
    } else {
      draw->PathArcTo(centre, r, from, to);
    }
  };
  corner(ImVec2(a.x + tl, a.y + tl), tl, kPi, 1.5f * kPi, a);
  corner(ImVec2(b.x - tr, a.y + tr), tr, 1.5f * kPi, 2.0f * kPi,
         ImVec2(b.x, a.y));
  corner(ImVec2(b.x - br, b.y - br), br, 0.0f, 0.5f * kPi, b);
  corner(ImVec2(a.x + bl, b.y - bl), bl, 0.5f * kPi, kPi, ImVec2(a.x, b.y));
}

void label_text(const Ui &ui, ImVec2 pos, const std::string &value,
                ImU32 colour, float px = 11.0f, float tracking = kLabelTracking,
                ImFont *font = nullptr) {
  tracked_text(ui.draw, ui.face, font ? font : ui.face.semibold, ui.css(px),
               pos, colour, upper(value), tracking);
}

float label_width(const Ui &ui, const std::string &value, float px = 11.0f,
                  float tracking = kLabelTracking, ImFont *font = nullptr) {
  return tracked_width(ui.face, font ? font : ui.face.semibold, ui.css(px),
                       upper(value), tracking);
}

// --- controls -----------------------------------------------------------------------

enum class Variant { filled, tonal, outlined, text, warn };

float button_width(const Ui &ui, const std::string &label, float px = 14.0f,
                   float pad = 16.0f) {
  return measure(ui.face.semibold, ui.css(px), label).x + pad * 2.0f;
}

// A Materialize button: a pill, a state layer when hovered, the M3 disabled
// treatment when it cannot be pressed.
bool button(const Ui &ui, const char *id, ImVec2 min, ImVec2 size,
            const std::string &label, Variant variant, bool enabled = true,
            float px = 14.0f) {
  ImGui::SetCursorScreenPos(min);
  ImGui::PushID(id);
  const bool pressed = ImGui::InvisibleButton("##b", size) && enabled;
  const bool hovered = enabled && ImGui::IsItemHovered();
  const bool held = enabled && ImGui::IsItemActive();
  ImGui::PopID();
  if (enabled && ImGui::IsItemHovered()) {
    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
  }

  const ImVec2 max(min.x + size.x, min.y + size.y);
  const float r = size.y * 0.5f;
  const Palette &p = ui.p;
  ImU32 bg = 0;
  ImU32 fg = p.accent;
  ImU32 edge = 0;
  switch (variant) {
  case Variant::filled:
    bg = p.accent;
    fg = p.on_accent;
    break;
  case Variant::tonal:
    bg = p.tonal;
    fg = p.on_tonal;
    break;
  case Variant::outlined:
    edge = p.outline;
    fg = p.accent;
    break;
  case Variant::text:
    fg = p.accent;
    break;
  case Variant::warn:
    bg = p.warn;
    fg = p.on_warn;
    break;
  }
  if (!enabled) {
    // Material's disabled roles: 12% of the text colour behind 38% of it.
    bg = (variant == Variant::text || variant == Variant::outlined)
             ? 0
             : faded(p.text, 0.12f);
    edge = variant == Variant::outlined ? faded(p.text, 0.12f) : 0;
    fg = faded(p.text, 0.38f);
  }
  if (bg != 0) {
    ui.draw->AddRectFilled(min, max, bg, r);
  }
  if (hovered || held) {
    ui.draw->AddRectFilled(min, max, faded(fg, held ? 0.16f : 0.08f), r);
  }
  if (edge != 0) {
    ui.draw->AddRect(min, max, edge, r, 1.0f);
  }
  const float size_px = ui.css(px);
  const ImVec2 text_size = measure(ui.face.semibold, size_px, label);
  ui.draw->AddText(ui.face.semibold, size_px,
                   ImVec2(min.x + (size.x - text_size.x) * 0.5f,
                          centred(min.y, size.y, size_px)),
                   fg, label.c_str());
  return pressed;
}

struct FieldOptions {
  const char *hint = "";
  bool password = false;
  bool readout = false; // a frequency: tabular, in the accent
  bool enabled = true;
};

// A sunken pill entry field, the FMS scratchpad look. Returns true when Enter
// was pressed in it.
bool field(const Ui &ui, const char *id, ImVec2 min, ImVec2 size, char *buffer,
           std::size_t capacity, const FieldOptions &options) {
  const Palette &p = ui.p;
  const float px = ui.css(15.0f);
  const float pad_x = 14.0f;
  const ImVec2 max(min.x + size.x, min.y + size.y);
  const float r = size.y * 0.5f;

  ImGui::PushID(id);
  const ImGuiID input_id = ImGui::GetID("##f");
  const bool focused = ImGui::GetActiveID() == input_id;
  const float alpha = options.enabled ? 1.0f : 0.5f;
  ui.draw->AddRectFilled(min, max, faded(p.glass_well, alpha), r);
  if (focused) {
    // The focus ring: the accent edge with a soft halo, as the CSS had it.
    ui.draw->AddRect(ImVec2(min.x - 3.0f, min.y - 3.0f),
                     ImVec2(max.x + 3.0f, max.y + 3.0f),
                     faded(p.accent, 0.22f), r + 3.0f, 3.0f);
    ui.draw->AddRect(min, max, p.accent, r, 1.0f);
  } else {
    ui.draw->AddRect(min, max, faded(p.border_strong, alpha), r, 1.0f);
  }

  ImGui::PushFont(ui.face.regular, px);
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                      ImVec2(0.0f, (size.y - px) * 0.5f));
  ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
  ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_Text,
                        faded(options.readout ? p.accent : p.text, alpha));
  ImGui::PushStyleColor(ImGuiCol_TextDisabled, faded(p.text_dim, 0.7f * alpha));
  ImGui::PushStyleColor(ImGuiCol_TextSelectedBg, faded(p.accent, 0.30f));
  ImGui::PushStyleColor(ImGuiCol_InputTextCursor, p.accent);
  ImGui::SetCursorScreenPos(ImVec2(min.x + pad_x, min.y));
  ImGui::SetNextItemWidth(size.x - pad_x * 2.0f);
  ImGuiInputTextFlags flags = ImGuiInputTextFlags_EnterReturnsTrue;
  if (options.password) {
    flags |= ImGuiInputTextFlags_Password;
  }
  if (!options.enabled) {
    ImGui::BeginDisabled();
  }
  const bool entered =
      ImGui::InputTextWithHint("##f", options.hint, buffer, capacity, flags);
  if (!options.enabled) {
    ImGui::EndDisabled();
  }
  ImGui::PopStyleColor(7);
  ImGui::PopStyleVar(2);
  ImGui::PopFont();
  ImGui::PopID();
  if (entered) {
    // Enter sends and gives the keyboard back to the sim: a pilot types a
    // transmission, not an essay, and their next key is probably a command.
    ImGui::ClearActiveID();
  }
  return entered;
}

std::string trimmed(const char *buffer) {
  std::string value(buffer);
  const auto first = value.find_first_not_of(" \t");
  if (first == std::string::npos) {
    return "";
  }
  const auto last = value.find_last_not_of(" \t");
  return value.substr(first, last - first + 1);
}

void set_buffer(char *buffer, std::size_t capacity, const std::string &value) {
  std::snprintf(buffer, capacity, "%s", value.c_str());
}

// Wrapped prose, held to a reading measure. Returns its height.
float paragraph(const Ui &ui, ImVec2 pos, float width, const std::string &value,
                ImU32 colour, float px = 14.0f, float line_height = 1.5f) {
  const float size = ui.css(px);
  const float line = px * line_height;
  float y = pos.y;
  for (const std::string &part :
       wrap_lines(ui.face.regular, size, value, width)) {
    ui.draw->AddText(ui.face.regular, size,
                     ImVec2(pos.x, centred(y, line, size)), colour,
                     part.c_str());
    y += line;
  }
  return y - pos.y;
}

float paragraph_height(const Ui &ui, float width, const std::string &value,
                       float px = 14.0f, float line_height = 1.5f) {
  return static_cast<float>(
             wrap_lines(ui.face.regular, ui.css(px), value, width).size()) *
         px * line_height;
}

// --- the mast -------------------------------------------------------------------------

constexpr float kMastHeight = 44.0f;

void status_chip(const Ui &ui, ImVec2 max_right, bool ready,
                 const std::string &value, float *out_left) {
  const Palette &p = ui.p;
  const float px = ui.css(12.0f);
  const float height = 26.0f;
  const float width =
      10.0f + 6.0f + 7.0f + measure(ui.face.semibold, px, value).x + 12.0f;
  const ImVec2 min(max_right.x - width, centred(max_right.y, kMastHeight, height));
  const ImVec2 max(max_right.x, min.y + height);
  ui.draw->AddRectFilled(min, max, p.glass_well, height * 0.5f);
  ui.draw->AddRect(min, max, ready ? p.border : faded(p.warn, 0.4f),
                   height * 0.5f, 1.0f);
  const ImU32 light = ready ? p.active : p.warn;
  const ImVec2 dot(min.x + 10.0f + 3.0f, min.y + height * 0.5f);
  ui.draw->AddCircleFilled(dot, 6.0f, faded(light, 0.18f));
  ui.draw->AddCircleFilled(dot, 3.0f, light);
  ui.draw->AddText(ui.face.semibold, px,
                   ImVec2(min.x + 10.0f + 6.0f + 7.0f, centred(min.y, height, px)),
                   ready ? p.text_feed : p.warn, value.c_str());
  *out_left = min.x;
}

// The round handle the HUD uses, so the two read as one instrument.
bool round_button(const Ui &ui, const char *id, ImVec2 centre, float diameter,
                  Icon which, ImU32 colour) {
  const ImVec2 min(centre.x - diameter * 0.5f, centre.y - diameter * 0.5f);
  ImGui::SetCursorScreenPos(min);
  const bool pressed = ImGui::InvisibleButton(id, ImVec2(diameter, diameter));
  const bool hovered = ImGui::IsItemHovered();
  if (hovered) {
    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    ui.draw->AddCircleFilled(centre, diameter * 0.5f, faded(colour, 0.16f));
  }
  ui.draw->AddCircle(centre, diameter * 0.5f, ui.p.border_strong, 0, 1.0f);
  const float size = diameter * 0.5f;
  icon(ui.draw, which, ImVec2(centre.x - size * 0.5f, centre.y - size * 0.5f),
       size, colour);
  return pressed;
}

void mast(const Ui &ui, PanelSession &session, ImVec2 min, float width,
          PanelViewResult &result) {
  const Palette &p = ui.p;
  // The close handle, then the console's verdict, from the right.
  const float right = min.x + width;
  const float close_d = 30.0f;
  if (round_button(ui, "close", ImVec2(right - close_d * 0.5f, min.y + kMastHeight * 0.5f),
                   close_d, Icon::close, p.text_dim)) {
    result.close = true;
  }
  float chip_left = 0.0f;
  status_chip(ui, ImVec2(right - close_d - 10.0f, min.y), session.reachable(),
              "Console " + gui::describe_link(session.link()), &chip_left);

  const float eyebrow_px = 11.0f;
  const float title_px = 20.0f;
  const float eyebrow_box = eyebrow_px * 1.4f;
  const float title_box = title_px * 1.2f;
  const float top = centred(min.y, kMastHeight, eyebrow_box + 1.0f + title_box);
  label_text(ui, ImVec2(min.x + 4.0f, centred(top, eyebrow_box, ui.css(eyebrow_px))),
             "zoal-atc " + ui.frame.version, p.text_dim);
  const float ty = top + eyebrow_box + 1.0f;
  // A bezel caption, not a landing page's headline.
  tracked_text(ui.draw, ui.face, ui.face.bold, ui.css(title_px),
               ImVec2(min.x + 4.0f, centred(ty, title_box, ui.css(title_px))),
               p.text, "Radio Panel", -0.01f);
}

// --- tabs: a segmented control -----------------------------------------------------------

constexpr float kTabsHeight = 36.0f;

void tabs(const Ui &ui, PanelViewState &state, ImVec2 min) {
  const Palette &p = ui.p;
  const char *labels[] = {"Home", "Settings"};
  const float px = ui.css(13.0f);
  float width = 3.0f * 2.0f + 2.0f;
  float widths[2];
  for (int i = 0; i < 2; ++i) {
    widths[i] = measure(ui.face.semibold, px, labels[i]).x + 32.0f;
    width += widths[i];
  }
  const ImVec2 max(min.x + width, min.y + kTabsHeight);
  ui.draw->AddRectFilled(min, max, p.glass_well, kTabsHeight * 0.5f);
  ui.draw->AddRect(min, max, p.border, kTabsHeight * 0.5f, 1.0f);
  float x = min.x + 4.0f;
  for (int i = 0; i < 2; ++i) {
    const ImVec2 bmin(x, min.y + 4.0f);
    const ImVec2 bsize(widths[i], kTabsHeight - 8.0f);
    const ImVec2 bmax(bmin.x + bsize.x, bmin.y + bsize.y);
    ImGui::SetCursorScreenPos(bmin);
    ImGui::PushID(i);
    if (ImGui::InvisibleButton("tab", bsize)) {
      state.tab = i;
    }
    const bool hovered = ImGui::IsItemHovered();
    ImGui::PopID();
    const bool selected = state.tab == i;
    ImU32 fg = p.text_dim;
    if (selected) {
      ui.draw->AddRectFilled(bmin, bmax, p.accent_container, bsize.y * 0.5f);
      fg = p.on_accent_container;
    } else if (hovered) {
      ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
      ui.draw->AddRectFilled(bmin, bmax, faded(p.accent, 0.08f), bsize.y * 0.5f);
      fg = p.text;
    }
    const float tw = measure(ui.face.semibold, px, labels[i]).x;
    ui.draw->AddText(ui.face.semibold, px,
                     ImVec2(bmin.x + (bsize.x - tw) * 0.5f, centred(bmin.y, bsize.y, px)),
                     fg, labels[i]);
    x += widths[i] + 2.0f;
  }
}

// --- the stale banner: a caution annunciator -------------------------------------------

float banner_height() { return 13.0f * 1.4f + 14.0f; }

void banner(const Ui &ui, ImVec2 min, float width, const std::string &message) {
  const Palette &p = ui.p;
  const ImVec2 max(min.x + width, min.y + banner_height());
  ui.draw->AddRectFilled(min, max, p.warn_surface, kRadius);
  ui.draw->AddRect(min, max, faded(p.warn, 0.4f), kRadius, 1.0f);
  const float icon_size = 14.0f;
  icon(ui.draw, Icon::warn, ImVec2(min.x + 10.0f, centred(min.y, max.y - min.y, icon_size)),
       icon_size, p.warn);
  tracked_text(ui.draw, ui.face, ui.face.semibold, ui.css(13.0f),
               ImVec2(min.x + 10.0f + icon_size + 8.0f,
                      centred(min.y, max.y - min.y, ui.css(13.0f))),
               p.warn, message, 0.02f);
}

// --- Home ---------------------------------------------------------------------------------

struct TileAction {
  std::string label;
  bool enabled = true;
  std::function<void()> run;
  Variant variant = Variant::text;
};

void tile(const Ui &ui, const char *id, ImVec2 min, ImVec2 size,
          const std::string &label, const std::string &value,
          const std::string &detail, bool dim, const TileAction *action) {
  const Palette &p = ui.p;
  const ImVec2 max(min.x + size.x, min.y + size.y);
  ui.draw->AddRectFilled(min, max, p.glass_card, kRadiusLg);
  ui.draw->AddRect(min, max, p.border, kRadiusLg, 1.0f);
  const float inner = size.x - 24.0f;

  const float label_box = 16.0f;
  float y = min.y + 9.0f;
  label_text(ui, ImVec2(min.x + 12.0f, centred(y, label_box, ui.css(11.0f))),
             label, p.text_dim);
  if (action != nullptr) {
    // The one obvious thing to do with a readout, on its label line so it
    // costs no height.
    const float bw = button_width(ui, action->label, 12.0f, 8.0f);
    const ImVec2 bmin(max.x - 6.0f - bw, y - 2.0f);
    if (button(ui, id, bmin, ImVec2(bw, 20.0f), action->label, action->variant,
               action->enabled, 12.0f)) {
      ui.later(action->run);
    }
  }
  y += label_box + 3.0f;

  const float value_px = ui.css(19.0f);
  const float value_box = 19.0f * 1.35f;
  ui.draw->AddText(ui.face.semibold, value_px,
                   ImVec2(min.x + 12.0f, centred(y, value_box, value_px)),
                   faded(p.text, dim ? 0.45f : 1.0f),
                   fit(ui.face.semibold, value_px, value, inner).c_str());
  y += value_box + 3.0f;
  if (!detail.empty()) {
    const float detail_px = ui.css(12.0f);
    ui.draw->AddText(ui.face.regular, detail_px,
                     ImVec2(min.x + 12.0f, centred(y, 12.0f * 1.4f, detail_px)),
                     p.text_dim,
                     fit(ui.face.regular, detail_px, detail, inner).c_str());
  }
}

struct Bubble {
  std::vector<std::string> lines;
  std::string speaker;
  std::string tag;
  float width = 0.0f;  // the bubble
  float row = 0.0f;    // the bubble or its label, whichever is wider
  float height = 0.0f; // label, gap and bubble
};

constexpr float kBubblePadX = 13.0f;
constexpr float kBubblePadY = 8.0f;
constexpr float kBubblePx = 15.0f;
constexpr float kBubbleLine = 15.0f * 1.45f;
constexpr float kSpeakerBox = 16.0f;
constexpr float kSpeakerGap = 3.0f;

Bubble measure_bubble(const Ui &ui, const HudEntry &entry,
                      const std::string &own_callsign, float max_width) {
  Bubble b;
  b.speaker = upper(gui::hud_speaker(entry, own_callsign));
  if (entry.overheard) {
    b.tag = upper(entry.addressed_to.empty() ? "overheard"
                                             : "to " + entry.addressed_to);
  }
  const float px = ui.css(kBubblePx);
  b.lines = wrap_lines(ui.face.regular, px, entry.text,
                       max_width - kBubblePadX * 2.0f);
  float words = 0.0f;
  for (const std::string &line : b.lines) {
    words = std::max(words, measure(ui.face.regular, px, line).x);
  }
  b.width = std::min(max_width, std::ceil(words) + kBubblePadX * 2.0f);
  float label = label_width(ui, b.speaker, 11.0f, kLabelTracking, ui.face.bold) + 8.0f;
  if (!b.tag.empty()) {
    label += 6.0f + label_width(ui, b.tag, 10.0f, 0.06f, ui.face.bold) + 14.0f;
  }
  b.row = std::max(b.width, label);
  b.height = kSpeakerBox + kSpeakerGap +
             kBubbleLine * static_cast<float>(b.lines.size()) +
             kBubblePadY * 2.0f;
  return b;
}

// One transmission, as a chat bubble: who spoke is carried by side, by colour
// and by a name above it -- three cues, because this is read at a glance.
// The HUD draws the same bubble (hud_window.cpp); one radio, one look.
void bubble(const Ui &ui, const HudEntry &entry, const Bubble &b, float left,
            float right, float y) {
  const Palette &p = ui.p;
  const bool from_pilot = entry.kind != "atc";
  const ImU32 seat = seat_colour(p, gui::hud_tone(entry));

  // The speaker, on the bubble's own side.
  float label_w = label_width(ui, b.speaker, 11.0f, kLabelTracking, ui.face.bold);
  float tag_w = 0.0f;
  if (!b.tag.empty()) {
    tag_w = label_width(ui, b.tag, 10.0f, 0.06f, ui.face.bold) + 14.0f;
  }
  const float whole = label_w + (tag_w > 0.0f ? 6.0f + tag_w : 0.0f);
  float x = from_pilot ? right - 4.0f - whole : left + 4.0f;
  label_text(ui, ImVec2(x, centred(y, kSpeakerBox, ui.css(11.0f))), b.speaker,
             seat, 11.0f, kLabelTracking, ui.face.bold);
  if (tag_w > 0.0f) {
    x += label_w + 6.0f;
    const ImVec2 tmin(x, y);
    const ImVec2 tmax(x + tag_w, y + kSpeakerBox);
    ui.draw->AddRectFilled(tmin, tmax, p.raised, kSpeakerBox * 0.5f);
    label_text(ui, ImVec2(x + 7.0f, centred(y, kSpeakerBox, ui.css(10.0f))),
               b.tag, p.text_dim, 10.0f, 0.06f, ui.face.bold);
  }
  y += kSpeakerBox + kSpeakerGap;

  const float bx = from_pilot ? right - b.width : left;
  const ImVec2 bmin(bx, y);
  const ImVec2 bmax(bx + b.width, y + b.height - kSpeakerBox - kSpeakerGap);
  // The speaker's corner tucked in.
  const float tl = from_pilot ? kRadiusLg : 4.0f;
  const float tr = from_pilot ? 4.0f : kRadiusLg;
  ImU32 fill = p.surface_comm;
  ImU32 edge = mix(seat & ~IM_COL32_A_MASK, seat, 0.30f);
  ImU32 words = p.text;
  if (entry.overheard) {
    // Another aircraft's clearance must not read as this cockpit's, or a
    // routing bug looks like normal operation.
    fill = 0;
    words = p.text_feed;
  } else if (from_pilot) {
    fill = p.pilot_bubble;
    edge = p.pilot_border;
  }
  if (fill != 0) {
    rounded_path(ui.draw, bmin, bmax, tl, tr, kRadiusLg, kRadiusLg);
    ui.draw->PathFillConvex(fill);
  }
  if (entry.overheard) {
    const ImDrawFlags corners =
        ImDrawFlags_RoundCornersAll &
        ~(from_pilot ? ImDrawFlags_RoundCornersTopRight
                     : ImDrawFlags_RoundCornersTopLeft);
    dashed_rect(ui.draw, bmin, bmax, p.border_strong, kRadiusLg, corners);
  } else {
    rounded_path(ui.draw, ImVec2(bmin.x + 0.5f, bmin.y + 0.5f),
                 ImVec2(bmax.x - 0.5f, bmax.y - 0.5f), tl, tr, kRadiusLg,
                 kRadiusLg);
    ui.draw->PathStroke(edge, 1.0f, ImDrawFlags_Closed);
  }
  const float px = ui.css(kBubblePx);
  float ly = bmin.y + kBubblePadY;
  for (const std::string &line : b.lines) {
    ui.draw->AddText(ui.face.regular, px,
                     ImVec2(bmin.x + kBubblePadX, centred(ly, kBubbleLine, px)),
                     words, line.c_str());
    ly += kBubbleLine;
  }
}

void comm_log(const Ui &ui, PanelSession &session, PanelViewState &state,
              ImVec2 min, ImVec2 size) {
  const Palette &p = ui.p;
  const auto &entries = session.comm();

  // New lines: follow them if the pilot is at the bottom, count them if not.
  bool follow = false;
  if (session.comm_revision() != state.comm_revision) {
    if (state.stick) {
      follow = true;
      state.pending_below = 0;
    } else if (entries.size() > state.comm_count) {
      state.pending_below += static_cast<int>(entries.size() - state.comm_count);
    } else {
      state.pending_below += 1;
    }
    state.comm_revision = session.comm_revision();
    state.comm_count = entries.size();
  }
  if (state.jump) {
    state.jump = false;
    state.stick = true;
    state.pending_below = 0;
    follow = true;
  }

  ImGui::SetCursorScreenPos(min);
  ImGui::BeginChild("log", size, ImGuiChildFlags_None,
                    ImGuiWindowFlags_NoBackground);
  ImDrawList *draw = ImGui::GetWindowDrawList();
  Ui inner{ui.face, ui.p, draw, ui.frame};

  const ImVec2 origin = ImGui::GetCursorScreenPos();
  const float width = ImGui::GetContentRegionAvail().x;
  if (entries.empty()) {
    // An empty radio says what fills it, rather than nothing at all.
    const std::string first = "No transmissions yet";
    const std::string second =
        "Hold push-to-talk, or type a transmission below.";
    const float fp = ui.css(15.0f);
    const float sp = ui.css(13.0f);
    const float cy = origin.y + size.y * 0.5f - 22.0f;
    const float fw = measure(ui.face.semibold, fp, first).x;
    const float sw = measure(ui.face.regular, sp, second).x;
    draw->AddText(ui.face.semibold, fp,
                  ImVec2(origin.x + (width - fw) * 0.5f, cy), p.text_feed,
                  first.c_str());
    draw->AddText(ui.face.regular, sp,
                  ImVec2(origin.x + (width - sw) * 0.5f, cy + 24.0f),
                  p.text_dim, second.c_str());
    ImGui::Dummy(ImVec2(width, 1.0f));
  } else {
    const float max_width = std::floor(width * 0.82f);
    float y = origin.y + 2.0f;
    const float left = origin.x + 2.0f;
    const float right = origin.x + width - 2.0f;
    for (const HudEntry &entry : entries) {
      const Bubble b = measure_bubble(inner, entry, session.callsign(), max_width);
      // Only what is on screen is drawn; the rest only takes its height.
      const float top = ImGui::GetWindowPos().y;
      const float bottom = top + ImGui::GetWindowHeight();
      if (y + b.height >= top && y <= bottom) {
        bubble(inner, entry, b, left, right, y);
      }
      y += b.height + 10.0f;
    }
    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy(ImVec2(width, y - origin.y - 10.0f + 2.0f));
  }

  const float scroll = ImGui::GetScrollY();
  const float max_scroll = ImGui::GetScrollMaxY();
  if (follow) {
    ImGui::SetScrollHereY(1.0f);
  } else if (ImGui::IsWindowHovered() || ImGui::IsWindowFocused()) {
    state.stick = max_scroll - scroll <= kStickSlack;
    if (state.stick) {
      state.pending_below = 0;
    }
  }
  ImGui::EndChild();

  // Reading back through the log must not mean missing the call that just
  // came in.
  if (state.pending_below > 0) {
    const std::string label = state.pending_below == 1
                                  ? "1 new transmission"
                                  : std::to_string(state.pending_below) +
                                        " new transmissions";
    const float bw = button_width(ui, label, 13.0f, 14.0f) + 18.0f;
    const ImVec2 bmin(min.x + size.x - 12.0f - bw, min.y + size.y - 10.0f - 30.0f);
    ui.draw->AddRectFilled(ImVec2(bmin.x, bmin.y + 2.0f),
                           ImVec2(bmin.x + bw, bmin.y + 32.0f), p.shadow, 15.0f);
    if (button(ui, "jump", bmin, ImVec2(bw, 30.0f), label + "    ",
               Variant::filled, true, 13.0f)) {
      state.jump = true;
    }
    icon(ui.draw, Icon::arrow_down,
         ImVec2(bmin.x + bw - 14.0f - 12.0f, bmin.y + 9.0f), 12.0f, p.on_accent);
  }
}

void home(const Ui &ui, PanelSession &session, PanelViewState &state,
          ImVec2 min, ImVec2 max) {
  const Palette &p = ui.p;
  const float width = max.x - min.x;
  const bool dim = !session.reachable();
  const std::uint64_t steady = ui.frame.steady_ms;
  const std::int64_t wall = ui.frame.wall_ms;
  const auto &telemetry = session.telemetry();
  const bool busy = session.home().busy();

  // ---- readouts: six across when the window is wide, folding as it narrows ----
  int columns = static_cast<int>((width + kTileGap) / (kTileMin + kTileGap));
  columns = std::clamp(columns, 1, 6);
  if (columns == 4 || columns == 5) {
    columns = 3; // three and three rather than a ragged second row
  }
  const int rows = (6 + columns - 1) / columns;
  const float tile_w =
      (width - kTileGap * static_cast<float>(columns - 1)) / static_cast<float>(columns);

  // Armed is a warning colour: the next press throws a clearance away.
  TileAction new_flight{session.armed() ? "Confirm" : "New flight", !busy,
                        [&session, steady, wall] {
                          session.press_new_flight(steady, wall);
                        },
                        session.armed() ? Variant::warn : Variant::text};
  const auto &plan = session.plan();
  TileAction import{plan.has_value() && plan->has_plan ? "Reload" : "SimBrief",
                    !busy, [&session, steady] { session.import_plan(steady); }};

  struct Readout {
    const char *id;
    std::string label, value, detail;
    TileAction *action;
  };
  std::vector<Readout> readouts = {
      {"flight", "Flight",
       session.callsign().empty() ? "waiting" : session.callsign(),
       session.lifecycle(), &new_flight},
      {"com1", "COM1",
       gui::format_frequency(telemetry ? telemetry->frequency_mhz : 0.0),
       telemetry ? gui::format_standby(telemetry->standby_mhz) : "", nullptr},
      {"alt", "Altitude", telemetry ? gui::format_altitude(*telemetry) : gui::kUnknown,
       telemetry ? gui::format_groundspeed(telemetry->groundspeed_kts) : "",
       nullptr},
      {"traffic", "Traffic",
       telemetry ? gui::format_traffic(*telemetry) : gui::kUnknown,
       telemetry ? telemetry->airport : "", nullptr},
      {"facility", "Facility",
       gui::format_facility_count(session.facility_count().value_or(0)), "",
       nullptr},
      {"plan", "Filed plan", gui::format_plan_route(plan),
       gui::format_plan_detail(plan), &import},
  };
  for (std::size_t i = 0; i < readouts.size(); ++i) {
    const int col = static_cast<int>(i) % columns;
    const int row = static_cast<int>(i) / columns;
    const ImVec2 tmin(min.x + static_cast<float>(col) * (tile_w + kTileGap),
                      min.y + static_cast<float>(row) * (kTileHeight + kTileGap));
    const Readout &r = readouts[i];
    tile(ui, r.id, tmin, ImVec2(tile_w, kTileHeight), r.label, r.value,
         r.detail, dim, r.action);
  }
  float y = min.y + static_cast<float>(rows) * kTileHeight +
            static_cast<float>(rows - 1) * kTileGap + kGap;

  // ---- the radio: everything the readouts leave ----
  const ImVec2 cmin(min.x, y);
  const ImVec2 cmax(max.x, max.y);
  ui.draw->AddRectFilled(cmin, cmax, p.glass_card, kRadiusLg);
  ui.draw->AddRect(cmin, cmax, p.border, kRadiusLg, 1.0f);
  const float ix = cmin.x + 12.0f;
  const float iw = cmax.x - cmin.x - 24.0f;
  float cy = cmin.y + 10.0f;

  const float head = 16.0f;
  label_text(ui, ImVec2(ix + 2.0f, centred(cy, head, ui.css(11.0f))), "Radio",
             p.text_dim);
  // The receive light, beside the radio's name.
  const ImU32 led = session.reachable() ? p.active : p.warn;
  const ImVec2 led_at(ix + 2.0f + label_width(ui, "Radio") + 10.0f, cy + head * 0.5f);
  ui.draw->AddCircleFilled(led_at, 7.0f, faded(led, 0.15f));
  ui.draw->AddCircleFilled(led_at, 4.0f, led);
  const std::string dropped = gui::format_dropped_events(session.link().dropped_events);
  if (!dropped.empty()) {
    const float dw = label_width(ui, dropped);
    label_text(ui, ImVec2(ix + iw - 2.0f - dw, centred(cy, head, ui.css(11.0f))),
               dropped, p.text_dim);
  }
  cy += head + 8.0f;
  ui.draw->AddLine(ImVec2(ix, cy), ImVec2(ix + iw, cy), p.border, 1.0f);
  cy += 8.0f;

  const std::string &error = session.home().error;
  const float error_h = error.empty() ? 0.0f : paragraph_height(ui, iw, error) + 8.0f;
  const float composer_h = 1.0f + 8.0f + kInputHeight;
  const float log_bottom = cmax.y - 12.0f - composer_h - 8.0f - error_h;
  comm_log(ui, session, state, ImVec2(ix, cy),
           ImVec2(iw, std::max(40.0f, log_bottom - cy)));

  float by = log_bottom + 8.0f;
  if (!error.empty()) {
    paragraph(ui, ImVec2(ix, by), iw, error, p.danger);
    by += error_h;
  }

  // ---- the composer: docked to the log, as a chat's input is ----
  ui.draw->AddLine(ImVec2(ix, by), ImVec2(ix + iw, by), p.border, 1.0f);
  by += 1.0f + 8.0f;
  const float tune_w = button_width(ui, "Tune");
  const float send_w = button_width(ui, "Send");
  const float bdy = (kInputHeight - kButtonHeight) * 0.5f;
  float x = ix;

  // Tuning is a radio action, so it lives with the radio. Narrow: a frequency
  // is six characters and a transmission is a sentence.
  bool tune = field(ui, "tune", ImVec2(x, by), ImVec2(110.0f, kInputHeight),
                    state.tune, sizeof(state.tune), {"118.800", false, true});
  x += 110.0f + 6.0f;
  tune |= button(ui, "tune_button", ImVec2(x, by + bdy),
                 ImVec2(tune_w, kButtonHeight), "Tune", Variant::tonal, !busy);
  x += tune_w + 12.0f;
  if (tune && !busy) {
    const std::string value = trimmed(state.tune);
    if (!value.empty()) {
      ui.later([&session, value, steady] { session.tune(value, steady); });
      state.tune[0] = '\0';
    }
  }

  const float say_w = std::max(120.0f, ix + iw - x - 6.0f - send_w);
  bool send = field(ui, "say", ImVec2(x, by), ImVec2(say_w, kInputHeight),
                    state.say, sizeof(state.say),
                    {"Type a transmission\xE2\x80\xA6"});
  send |= button(ui, "send", ImVec2(x + say_w + 6.0f, by + bdy),
                 ImVec2(send_w, kButtonHeight), "Send", Variant::filled, !busy);
  if (send && !busy) {
    const std::string value = trimmed(state.say);
    if (!value.empty()) {
      ui.later([&session, value, steady] { session.transmit(value, steady); });
      state.say[0] = '\0';
    }
  }
}

// --- Settings -------------------------------------------------------------------------

struct Flow {
  const Ui &ui;
  float x;
  float width;
  float y;
  bool first_section = true;

  void section(const std::string &title) {
    const Palette &p = ui.p;
    if (!first_section) {
      y += 14.0f;
    }
    first_section = false;
    const float box = 12.0f * 1.4f;
    label_text(ui, ImVec2(x, centred(y, box, ui.css(12.0f))), title, p.accent,
               12.0f, 0.08f, ui.face.bold);
    y += box + 4.0f;
    ui.draw->AddLine(ImVec2(x, y), ImVec2(x + width, y), p.border_strong, 1.0f);
    y += 1.0f;
  }

  void begin_setting() { y += 10.0f; }

  void end_setting(bool last = false) {
    y += 10.0f;
    if (!last) {
      ui.draw->AddLine(ImVec2(x, y), ImVec2(x + width, y), ui.p.border, 1.0f);
      y += 1.0f;
    }
  }

  void gap() { y += 6.0f; }

  void label(const std::string &value) {
    const float box = 16.0f;
    label_text(ui, ImVec2(x + 2.0f, centred(y, box, ui.css(11.0f))), value,
               ui.p.text_dim);
    y += box;
  }

  void strong(const std::string &value) {
    const float px = ui.css(15.0f);
    const float box = 15.0f * 1.4f;
    ui.draw->AddText(ui.face.semibold, px, ImVec2(x + 2.0f, centred(y, box, px)),
                     ui.p.text, fit(ui.face.semibold, px, value, width).c_str());
    y += box;
  }

  void help(const std::string &value) {
    // Held to a reading measure: a 1400px line is read once and lost on the
    // way back.
    y += paragraph(ui, ImVec2(x + 2.0f, y), std::min(width - 4.0f, 680.0f),
                   value, ui.p.text_dim);
  }

  void error(const std::string &value) {
    if (value.empty()) {
      return;
    }
    gap();
    y += paragraph(ui, ImVec2(x + 2.0f, y), width - 4.0f, value, ui.p.danger);
  }
};

void theme_picker(Flow &flow, PanelSession &session) {
  const Ui &ui = flow.ui;
  const Palette &p = ui.p;
  const float gap = 8.0f;
  const float avail = std::min(flow.width, 780.0f);
  int columns = std::clamp(static_cast<int>((avail + gap) / (180.0f + gap)), 1, 3);
  const float card_w =
      (avail - gap * static_cast<float>(columns - 1)) / static_cast<float>(columns);
  // The text column beside the swatch, and the description wrapped into it:
  // every card as tall as the one with the most to say.
  const float text_x = 12.0f + 47.0f + 10.0f;
  const float text_w = card_w - text_x - 12.0f;
  const float desc_px = ui.css(12.0f);
  const float desc_line = 12.0f * 1.35f;
  std::size_t most_lines = 1;
  for (const ThemeOption &option : kThemes) {
    most_lines = std::max(
        most_lines,
        wrap_lines(ui.face.regular, desc_px, option.description, text_w).size());
  }
  const float block =
      14.0f * 1.4f + 2.0f + desc_line * static_cast<float>(most_lines);
  const float card_h = std::max(58.0f, block + 20.0f);
  const std::uint64_t steady = ui.frame.steady_ms;

  int index = 0;
  for (const ThemeOption &option : kThemes) {
    const int col = index % columns;
    const int row = index / columns;
    const ImVec2 min(flow.x + static_cast<float>(col) * (card_w + gap),
                     flow.y + static_cast<float>(row) * (card_h + gap));
    const ImVec2 max(min.x + card_w, min.y + card_h);
    ImGui::SetCursorScreenPos(min);
    ImGui::PushID(index);
    const bool pressed = ImGui::InvisibleButton("theme", ImVec2(card_w, card_h));
    const bool hovered = ImGui::IsItemHovered();
    ImGui::PopID();
    const bool selected = session.theme() == option.theme;
    if (pressed && !selected) {
      const HudTheme theme = option.theme;
      ui.later([&session, theme, steady] { session.pick_theme(theme, steady); });
    }
    if (hovered) {
      ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
    ui.draw->AddRectFilled(min, max, p.glass_card, kRadiusLg);
    if (selected) {
      ui.draw->AddRect(min, max, p.accent, kRadiusLg, 2.0f);
    } else {
      ui.draw->AddRect(min, max, hovered ? p.border_strong : p.border, kRadiusLg,
                       1.0f);
    }

    // The swatch shows the theme's own colours, whatever is being worn.
    const ImVec2 smin(min.x + 12.0f, centred(min.y, card_h, 21.0f));
    const ImVec2 smax(smin.x + 47.0f, smin.y + 21.0f);
    ui.draw->AddRectFilled(smin, smax, rgba(option.bg), 10.5f);
    ui.draw->AddRect(smin, smax, IM_COL32(128, 128, 128, 77), 10.5f, 1.0f);
    const unsigned dots[] = {option.text, option.accent, option.active};
    for (int d = 0; d < 3; ++d) {
      ui.draw->AddCircleFilled(
          ImVec2(smin.x + 7.0f + 4.5f + static_cast<float>(d) * 12.0f,
                 smin.y + 10.5f),
          4.5f, rgba(dots[d]));
    }

    const float tx = min.x + text_x;
    const float name_px = ui.css(14.0f);
    const float top = centred(min.y, card_h, block);
    ui.draw->AddText(ui.face.semibold, name_px,
                     ImVec2(tx, centred(top, 14.0f * 1.4f, name_px)), p.text,
                     option.name);
    float dy = top + 14.0f * 1.4f + 2.0f;
    for (const std::string &line :
         wrap_lines(ui.face.regular, desc_px, option.description, text_w)) {
      ui.draw->AddText(ui.face.regular, desc_px,
                       ImVec2(tx, centred(dy, desc_line, desc_px)), p.text_dim,
                       line.c_str());
      dy += desc_line;
    }
    ++index;
  }
  const int rows = (index + columns - 1) / columns;
  flow.y += static_cast<float>(rows) * card_h + static_cast<float>(rows - 1) * gap;
}

// A label, an entry field and the buttons that act on it, in one row.
struct RowButton {
  std::string label;
  Variant variant;
  bool enabled;
  bool *pressed;
};

bool control_row(Flow &flow, const char *id, const std::string &label,
                 char *buffer, std::size_t capacity, const FieldOptions &options,
                 std::vector<RowButton> buttons) {
  const Ui &ui = flow.ui;
  const float label_w = 150.0f;
  const float x0 = flow.x;
  label_text(ui, ImVec2(x0 + 2.0f, centred(flow.y, kInputHeight, ui.css(11.0f))),
             label, ui.p.text_dim);
  float buttons_w = 0.0f;
  for (const RowButton &b : buttons) {
    buttons_w += button_width(ui, b.label) + 8.0f;
  }
  const float field_x = x0 + label_w;
  const float field_w = std::max(120.0f, flow.width - label_w - buttons_w);
  const bool entered = field(ui, id, ImVec2(field_x, flow.y),
                             ImVec2(field_w, kInputHeight), buffer, capacity, options);
  float bx = field_x + field_w + 8.0f;
  ImGui::PushID(id);
  for (const RowButton &b : buttons) {
    const float bw = button_width(ui, b.label);
    if (button(ui, b.label.c_str(), ImVec2(bx, flow.y + 1.0f),
               ImVec2(bw, kButtonHeight), b.label, b.variant, b.enabled)) {
      *b.pressed = true;
    }
    bx += bw + 8.0f;
  }
  ImGui::PopID();
  flow.y += kInputHeight;
  return entered;
}

void settings(const Ui &ui, PanelSession &session, PanelViewState &state,
              ImVec2 min, ImVec2 max) {
  const std::uint64_t steady = ui.frame.steady_ms;

  // A field showing a stored value reloads when the plugin's value changes:
  // once it is first read, and after a save.
  if (session.connection_loaded() &&
      (!state.token_loaded || session.token() != state.token_stored)) {
    state.token_loaded = true;
    state.token_stored = session.token();
    set_buffer(state.token, sizeof(state.token), state.token_stored);
  }
  if (session.settings_loaded() &&
      (!state.pilot_loaded || session.simbrief_pilot_id() != state.pilot_stored)) {
    state.pilot_loaded = true;
    state.pilot_stored = session.simbrief_pilot_id();
    set_buffer(state.pilot_id, sizeof(state.pilot_id), state.pilot_stored);
  }

  ImGui::SetCursorScreenPos(min);
  ImGui::BeginChild("settings", ImVec2(max.x - min.x, max.y - min.y),
                    ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
  Ui inner{ui.face, ui.p, ImGui::GetWindowDrawList(), ui.frame};
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  const float scrollbar = ImGui::GetScrollMaxY() > 0.0f ? 14.0f : 0.0f;
  Flow flow{inner, origin.x, ImGui::GetContentRegionAvail().x - scrollbar,
            origin.y};

  // ---- appearance ----
  flow.section("Appearance");
  flow.begin_setting();
  flow.label("Theme");
  flow.gap();
  theme_picker(flow, session);
  flow.gap();
  flow.help("Applies to this panel and the HUD, and is remembered for this "
            "aircraft.");
  flow.end_setting();

  // ---- connection ----
  flow.section("Connection");
  flow.begin_setting();
  flow.label("Console");
  flow.strong(session.console_url().empty() ? gui::kUnknown
                                            : session.console_url());
  flow.gap();
  flow.help("Where this plugin connects. Fixed here \xE2\x80\x94 change it in "
            "Output/preferences/zoal_atc.cfg if you run your own console.");
  flow.end_setting();

  flow.begin_setting();
  bool reveal = false;
  bool save_token = false;
  // Never gated on a save in flight: saving a token drops the socket on
  // purpose, so this is the request most likely to lose its answer, and
  // locking the field until then would take away the one control that
  // repairs a mistyped credential.
  save_token |= control_row(
      flow, "token", "Access token", state.token, sizeof(state.token),
      {"Leave empty if the console is open", !state.token_revealed, false,
       session.connection_loaded()},
      {{state.token_revealed ? "Hide" : "Show", Variant::outlined, true, &reveal},
       {"Save token", Variant::tonal, session.connection_loaded(), &save_token}});
  if (reveal) {
    state.token_revealed = !state.token_revealed;
  }
  if (save_token && session.connection_loaded()) {
    const std::string token = trimmed(state.token);
    ui.later([&session, token, steady] { session.save_token(token, steady); });
  }
  flow.gap();
  flow.help("Authorises this aircraft with the console. Saving reconnects, so "
            "the status above tells you straight away whether it was "
            "accepted. Unlike everything else here it is stored by the "
            "plugin, not the console \xE2\x80\x94 it has to work while the "
            "console is unreachable, because it is how it becomes reachable. "
            "Save it empty to clear it.");
  flow.error(session.local().error);
  flow.end_setting();

  // ---- flight plan ----
  flow.section("Flight plan");
  flow.begin_setting();
  bool save_pilot = false;
  save_pilot |= control_row(
      flow, "pilot", "SimBrief pilot ID", state.pilot_id, sizeof(state.pilot_id),
      {"Pilot ID", false, false, session.settings_loaded()},
      {{"Save", Variant::tonal, session.settings_loaded(), &save_pilot}});
  if (save_pilot && session.settings_loaded()) {
    const std::string id = trimmed(state.pilot_id);
    ui.later([&session, id, steady] { session.save_pilot_id(id, steady); });
  }
  flow.gap();
  flow.help("Remembered for this aircraft, so an import needs no ID. Two pilots "
            "connected to one console each keep their own.");
  flow.end_setting();

  flow.begin_setting();
  flow.label("Imported plan");
  flow.strong(gui::format_plan_route(session.plan()));
  flow.gap();
  const std::string detail = gui::format_plan_detail(session.plan());
  flow.help(detail.empty() ? "No plan imported yet." : detail);
  flow.gap();
  flow.gap();
  const float iw = button_width(inner, "Import now");
  if (button(inner, "import", ImVec2(flow.x + 2.0f, flow.y),
             ImVec2(iw, kButtonHeight), "Import now", Variant::filled,
             !session.settings().busy())) {
    ui.later([&session, steady] { session.import_now(steady); });
  }
  flow.y += kButtonHeight;
  flow.error(session.settings().error);
  flow.end_setting(true);

  ImGui::SetCursorScreenPos(origin);
  ImGui::Dummy(ImVec2(1.0f, flow.y - origin.y + 4.0f));
  ImGui::EndChild();
}

} // namespace

void setup_panel_style() {
  ImGuiStyle &style = ImGui::GetStyle();
  style.ItemSpacing = ImVec2(0.0f, 0.0f);
  style.WindowPadding = ImVec2(0.0f, 0.0f);
  style.WindowBorderSize = 0.0f;
  style.ChildBorderSize = 0.0f;
  style.ScrollbarSize = 8.0f;
  style.ScrollbarRounding = 4.0f;
  style.ScrollbarPadding = 1.0f;
  style.DisabledAlpha = 0.5f;
  ImGuiIO &io = ImGui::GetIO();
  // A window inside a sim: no ini file, no nav highlight on mouse use.
  io.ConfigWindowsMoveFromTitleBarOnly = true;
}

PanelViewResult draw_panel(PanelSession &session, PanelViewState &state,
                           const Typeface &face, const PanelFrame &frame) {
  PanelViewResult result;
  const Palette &p = palette(session.theme());
  ImGuiIO &io = ImGui::GetIO();
  const ImVec2 display = io.DisplaySize;

  ImGuiStyle &style = ImGui::GetStyle();
  style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
  style.Colors[ImGuiCol_ScrollbarGrab] = ImGui::ColorConvertU32ToFloat4(p.border_strong);
  style.Colors[ImGuiCol_ScrollbarGrabHovered] =
      ImGui::ColorConvertU32ToFloat4(faded(p.text_dim, 0.5f));
  style.Colors[ImGuiCol_ScrollbarGrabActive] =
      ImGui::ColorConvertU32ToFloat4(faded(p.text_dim, 0.7f));

  ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
  ImGui::SetNextWindowSize(display);
  ImGui::Begin("zoal-atc panel", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoScrollWithMouse |
                   ImGuiWindowFlags_NoBringToFrontOnFocus);
  ImDrawList *draw = ImGui::GetWindowDrawList();
  const Ui ui{face, p, draw, frame};

  // The glass: mostly opaque, so text reads over a bright sky, but not a wall.
  draw->AddRectFilled(ImVec2(0.0f, 0.0f), display, p.glass, kWindowRadius);
  draw->AddRect(ImVec2(0.0f, 0.0f), display, p.border_strong, kWindowRadius, 1.0f);

  const float width = display.x - kPadX * 2.0f;
  float y = kPadTop;
  mast(ui, session, ImVec2(kPadX, y), width, result);
  y += kMastHeight + kGap;

  tabs(ui, state, ImVec2(kPadX, y));
  y += kTabsHeight + kGap;

  const std::string stale = session.freshness_message(frame.wall_ms);
  if (!stale.empty()) {
    banner(ui, ImVec2(kPadX, y), width, stale);
    y += banner_height() + kGap;
  }

  const ImVec2 content_min(kPadX, y);
  const ImVec2 content_max(display.x - kPadX, display.y - kPadBottom);
  if (state.tab == 0) {
    home(ui, session, state, content_min, content_max);
  } else {
    settings(ui, session, state, content_min, content_max);
  }

  // The whole panel is its grip: a press on anything that is not a control --
  // a field, a button, a tab, a scrollbar -- moves the window.
  if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemHovered() &&
      !ImGui::IsAnyItemActive()) {
    state.moving = true;
    result.drag_started = true;
  }
  if (state.moving && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
    state.moving = false;
  }
  result.dragging = state.moving;
  if (state.moving) {
    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
  }

  ImGui::End();
  return result;
}

} // namespace zoal_atc::ui
