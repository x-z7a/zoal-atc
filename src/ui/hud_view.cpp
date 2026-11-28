#include "hud_view.hpp"

#include "hud_palette.hpp"
#include "kit.hpp"
#include "zoal_atc/gui/display_numbers.hpp"
#include "zoal_atc/gui/i18n.hpp"
#include "zoal_atc/gui/hud_layout.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "imgui.h"

// The radio HUD's drawing, with no X-Plane in it: src/xplane/hud_window.cpp
// draws it in the sim, and tools/panel_preview draws it off-screen so a change
// to the HUD can be looked at without starting the sim.

namespace zoal_atc::ui {

namespace {

using gui::HudAction;
using gui::tr;
using gui::HudLine;
using gui::HudSession;
using gui::HudTheme;
using gui::HudTone;

// What one frame draws with: set by draw_hud for the length of the frame.
struct Frame {
  const Typeface *face = nullptr;
  HudViewState *state = nullptr;
};
Frame g;

// A CSS px size as an ImGui font size, for the face the HUD is set in.
float css(float px) { return g.face->css(px); }

// --- sizes, in boxels: the React HUD's (hud.css), so the HUD looks the same --

constexpr float kPad = 2.0f;            // inside the window's edge
constexpr float kGap = 6.0f;            // between the row and the lines
constexpr float kBarHeight = 32.0f;
constexpr float kBarRadius = 16.0f;
constexpr float kLed = 8.0f;
constexpr float kHandle = 24.0f;
constexpr float kActionHeight = 26.0f;
constexpr float kIcon = 15.0f;
constexpr float kErrorMaxWidth = 380.0f;
constexpr float kBubblePadX = 13.0f;
constexpr float kBubblePadY = 8.0f;
constexpr float kBubbleRadius = 12.0f;
constexpr float kSpeakerGap = 3.0f;
// Type sizes are the React HUD's CSS sizes, in CSS px; css() converts them
// (see ui::Typeface::em_to_line).
constexpr float kTitlePx = 13.0f;
constexpr float kWarnPx = 11.0f;
constexpr float kActionPx = 12.0f;
constexpr float kBubblePx = 14.0f;
constexpr float kSpeakerPx = 11.0f;
constexpr float kTagPx = 10.0f;
// The bubbles' CSS line-height (1.45): ImGui would set lines at the font's own
// 1.17, which reads cramped in a two-line clearance.
constexpr float kBubbleLine = 14.0f * 1.45f;
// CSS letter-spacing on the small uppercase labels, in em.
constexpr float kTitleTracking = 0.04f;
constexpr float kWarnTracking = 0.06f;
constexpr float kSpeakerTracking = 0.07f;

// --- colours ---------------------------------------------------------------

using ui::blend;
using ui::faded;
using ui::fit;
using ui::Icon;
using ui::icon;
using ui::measure;
using ui::rgba;
using ui::upper;
using ui::wrap_lines;

using hud_style::edge;
using hud_style::glass;
using hud_style::Palette;
using hud_style::palette;

unsigned seat_colour(const Palette &p, HudTone tone) {
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

// --- drawing helpers ---------------------------------------------------------

// The HUD sits over the sky with no glass behind some of its text, so text
// carries a shadow in the opposite tone.
ImU32 shadow_of(const Palette &p, float opacity) {
  return faded(rgba(p.shadow, p.shadow_alpha), opacity);
}

void text(ImDrawList *draw, ImFont *font, float size, ImVec2 pos, ImU32 colour,
          const std::string &value, const Palette &p, float opacity = 1.0f,
          float wrap = 0.0f) {
  ui::text(draw, font, size, pos, faded(colour, opacity), value,
           shadow_of(p, opacity), wrap);
}

float tracked_width(ImFont *font, float size, const std::string &value,
                    float tracking) {
  return ui::tracked_width((*g.face), font, size, value, tracking);
}

void tracked_text(ImDrawList *draw, ImFont *font, float size, ImVec2 pos,
                  ImU32 colour, const std::string &value, const Palette &p,
                  float tracking, float opacity = 1.0f) {
  ui::tracked_text(draw, (*g.face), font, size, pos, faded(colour, opacity),
                   value, tracking, shadow_of(p, opacity));
}

// A hit area at `min`, `size` big. Returns whether it was clicked, and says
// whether the mouse is over it.
bool hit(const char *id, ImVec2 min, ImVec2 size, bool *hovered) {
  ImGui::SetCursorScreenPos(min);
  const bool clicked = ImGui::InvisibleButton(id, size);
  *hovered = ImGui::IsItemHovered();
  return clicked;
}

// --- the menu ----------------------------------------------------------------

struct ActionButton {
  const char *id;
  Icon icon;
  std::string label;
  bool enabled;
  bool armed;
  bool done;
  float width = 0.0f;
};

std::vector<ActionButton> action_buttons(const HudSession &session) {
  const auto &menu = session.menu();
  std::vector<ActionButton> buttons;
  const bool reloaded = menu.done() == HudAction::reload_plan;
  buttons.push_back({"reload", reloaded ? Icon::check : Icon::reload,
                     reloaded ? tr("Plan loaded") : tr("Reload plan"),
                     menu.reload_enabled(), false, reloaded});
  const bool started = menu.done() == HudAction::end_flight;
  buttons.push_back({"new_flight",
                     menu.armed() ? Icon::warn
                                  : (started ? Icon::check : Icon::plane),
                     menu.armed() ? tr("Confirm")
                                  : (started ? tr("Started") : tr("New flight")),
                     menu.new_flight_enabled(), menu.armed(), started});
  buttons.push_back({"open_panel", Icon::panel, tr("Open panel"),
                     menu.open_panel_enabled(), false, false});
  for (ActionButton &button : buttons) {
    button.width = 8.0f + kIcon + 5.0f +
                   measure(g.face->semibold, css(kActionPx), button.label).x + 10.0f;
  }
  return buttons;
}

// --- the call strip ----------------------------------------------------------
//
// What the position on the tuned channel would take, as buttons, in the order
// it expects to hear them (atc docs/architecture/pilot-menu.md): the readback
// first and filled, then the calls. The channel the pilot was sent to sits at
// the start and does not scroll; the calls scroll sideways, by the arrows at
// the strip's ends or the wheel, when there are more than fit.

constexpr float kCallHeight = 26.0f;
constexpr float kCallGap = 4.0f;
constexpr float kCallPadX = 11.0f;
constexpr float kCallPx = 12.0f;

struct CallButton {
  std::string key;
  std::string label;
  bool readback = false;
  bool in_flight = false;
  // A choice: pressing it shows its options rather than making a call.
  bool choice = false;
  bool open = false;
  float width = 0.0f;
};

// One pill: hovered, filled for the readback, dimmed while it is in flight.
// Returns whether it was clicked. `clip` is the visible part of the strip, so
// a button scrolled out of sight cannot be pressed.
bool call_pill(ImDrawList *draw, const Palette &p, const char *id, ImVec2 min,
               float width, const std::string &english, Icon glyph, bool filled,
               bool enabled, ImVec4 clip) {
  // A button's label in the pilot's language; what it says on the radio is
  // the console's and is not this.
  const std::string label = gui::tr(english);
  const ImVec2 max(min.x + width, min.y + kCallHeight);
  const ImVec2 hit_min(std::max(min.x, clip.x), min.y);
  const ImVec2 hit_max(std::min(max.x, clip.z), max.y);
  bool hovered = false;
  bool clicked = false;
  if (hit_max.x - hit_min.x > 1.0f) {
    clicked = hit(id, hit_min, ImVec2(hit_max.x - hit_min.x, kCallHeight), &hovered);
  }
  const float alpha = enabled ? 1.0f : 0.45f;
  ImU32 fg = rgba(p.text);
  ImU32 glyph_colour = rgba(p.accent);
  if (filled) {
    draw->AddRectFilled(min, max, faded(rgba(p.accent, 0.88f), alpha), kCallHeight * 0.5f);
    fg = rgba(p.sunken);
    glyph_colour = fg;
  } else {
    draw->AddRectFilled(min, max, faded(rgba(p.raised, 0.55f), alpha), kCallHeight * 0.5f);
    draw->AddRect(min, max, faded(edge(p), alpha), kCallHeight * 0.5f, 1.0f);
  }
  if (hovered && enabled) {
    draw->AddRect(min, max, rgba(p.accent, 0.9f), kCallHeight * 0.5f, 1.5f);
  }
  const float mid = min.y + kCallHeight * 0.5f;
  float tx = min.x + kCallPadX;
  if (glyph != Icon::menu) {
    icon(draw, glyph, ImVec2(tx, mid - kIcon * 0.5f), kIcon, faded(glyph_colour, alpha));
    tx += kIcon + 5.0f;
  }
  if (!label.empty()) {
    draw->AddText(g.face->semibold, css(kCallPx), ImVec2(tx, mid - css(kCallPx) * 0.5f),
                  faded(fg, alpha), label.c_str());
  }
  return clicked && enabled;
}

// A round button with a chevron, for scrolling the call strip. Dimmed and
// unpressable when there is nothing further that way.
bool scroll_arrow(ImDrawList *draw, const Palette &p, const char *id, ImVec2 min,
                  float size, bool ahead, bool enabled) {
  bool hovered = false;
  const bool clicked = hit(id, min, ImVec2(size, kCallHeight), &hovered);
  const ImVec2 centre(min.x + size * 0.5f, min.y + kCallHeight * 0.5f);
  const float alpha = enabled ? 1.0f : 0.3f;
  draw->AddCircleFilled(centre, size * 0.5f,
                        faded(rgba(p.raised, hovered && enabled ? 0.95f : 0.7f), alpha));
  draw->AddCircle(centre, size * 0.5f,
                  faded(hovered && enabled ? rgba(p.accent, 0.9f) : edge(p), alpha), 0, 1.0f);
  const float h = 5.0f;
  const float w = ahead ? 3.0f : -3.0f;
  const ImVec2 points[] = {ImVec2(centre.x - w, centre.y - h), ImVec2(centre.x + w, centre.y),
                           ImVec2(centre.x - w, centre.y + h)};
  draw->AddPolyline(points, 3, faded(rgba(p.accent), alpha), ImDrawFlags_None, 2.0f);
  return clicked && enabled;
}

float pill_width(const std::string &english, bool with_icon) {
  const std::string label = gui::tr(english);
  float width = kCallPadX * 2.0f;
  if (with_icon) {
    width += kIcon + (label.empty() ? 0.0f : 5.0f);
  }
  if (!label.empty()) {
    width += measure(g.face->semibold, css(kCallPx), label).x;
  }
  return width;
}

// strip_tail is what hangs under the strip: the open choice's options and
// the last press's error.
float strip_tail(ImDrawList *draw, HudSession &session, const Palette &p, float right,
                 float y, float strip_w, float *widest) {
  const float most = gui::HudPlacement{}.width - kPad * 2.0f;
  // The open choice's options, under the strip and as wide as it, wrapped:
  // eight at most, so they fit in two rows without scrolling.
  if (const gui::HudCallItem *open = session.open_choice()) {
    y += kGap;
    const float left_edge = right - strip_w;
    float ox = left_edge;
    float oy = y;
    const ImVec4 all(left_edge, y, right, y + 400.0f);
    int index = 0;
    for (const gui::HudCallItem &option : open->options) {
      const float w = pill_width(option.label, false);
      if (ox > left_edge && ox + w > right) {
        ox = left_edge;
        oy += kCallHeight + kCallGap;
      }
      const std::string id = "option" + std::to_string(index++);
      if (call_pill(draw, p, id.c_str(), ImVec2(ox, oy), w, option.label, Icon::menu, false,
                    !session.call_in_flight(option.key) && session.may_key_up(), all)) {
        g.state->pressed_call = option.key;
      }
      ox += w + kCallGap;
    }
    y = oy + kCallHeight;
  }

  if (!session.call_error().empty()) {
    const std::string shown = fit(g.face->semibold, css(kActionPx), session.call_error(),
                                  most - 20.0f);
    const float w = measure(g.face->semibold, css(kActionPx), shown).x;
    y += 4.0f;
    bool hovered = false;
    if (hit("call_error", ImVec2(right - w - 4.0f, y), ImVec2(w + 4.0f, 18.0f), &hovered)) {
      g.state->pressed_dismiss_call = true;
    }
    text(draw, g.face->semibold, css(kActionPx), ImVec2(right - w - 2.0f, y + 2.0f),
         rgba(p.danger), shown, p);
    y += 18.0f;
    *widest = std::max(*widest, w + 4.0f);
  }
  return y;
}

float call_strip(ImDrawList *draw, HudSession &session, const Palette &p,
                 float right, float y, float *widest) {
  const gui::HudCallMenu *menu = session.calls();
  const bool has_next = menu != nullptr && menu->next_frequency_hz != 0 &&
                        menu->next_frequency_hz != session.active_hz();
  if ((menu == nullptr || menu->items.empty()) && !has_next) {
    return y;
  }

  // The buttons that do not scroll.
  const std::string next_label = has_next ? gui::format_mhz(menu->next_frequency_hz) : "";
  float fixed_w = 0.0f;
  if (has_next) {
    fixed_w += pill_width(next_label, true) + kCallGap;
  }

  std::vector<CallButton> calls;
  float calls_w = 0.0f;
  if (menu != nullptr) {
    for (const gui::HudCallItem &item : menu->items) {
      // Shut while another voice is on the channel (x-z7a/atc#1143).
      CallButton button{item.key, item.label, item.readback,
                        session.call_in_flight(item.key) || !session.may_key_up()};
      button.choice = !item.options.empty();
      button.open = button.choice && session.open_choice() != nullptr &&
                    session.open_choice()->key == item.key;
      button.width = pill_width(button.label, button.readback || button.choice);
      calls_w += button.width + kCallGap;
      calls.push_back(std::move(button));
    }
  }
  if (!calls.empty()) {
    calls_w -= kCallGap;
  }

  const float most = gui::HudPlacement{}.width - kPad * 2.0f;
  const float pad = 3.0f;

  if (!session.shows_transcript()) {
    // No transcript under it, so the room is the strip's: every call in rows,
    // as many as it takes, with nothing to scroll.
    const float strip_w = std::min(most, fixed_w + calls_w + pad * 2.0f);
    const float inner_left = right - strip_w + pad;
    const float inner_right = right - pad;
    std::vector<ImVec2> at;
    float cx = inner_left;
    float cy = y + kGap + pad;
    auto place = [&](float w) {
      if (cx > inner_left && cx + w > inner_right) {
        cx = inner_left;
        cy += kCallHeight + kCallGap;
      }
      at.push_back(ImVec2(cx, cy));
      cx += w + kCallGap;
    };
    if (has_next) {
      place(pill_width(next_label, true));
    }
    for (const CallButton &button : calls) {
      place(button.width);
    }
    y += kGap;
    const float strip_h = cy + kCallHeight + pad - y;
    const ImVec2 min(right - strip_w, y);
    const ImVec2 max(right, y + strip_h);
    const float radius = std::min(strip_h * 0.5f, 16.0f);
    draw->AddRectFilled(min, max, glass(p), radius);
    draw->AddRect(min, max, edge(p), radius, 1.0f);
    const ImVec4 everything(min.x, min.y, max.x, max.y);
    std::size_t n = 0;
    if (has_next) {
      if (call_pill(draw, p, "next_frequency", at[n++], pill_width(next_label, true),
                    next_label, Icon::arrow_down, false, true, everything)) {
        g.state->pressed_next = true;
      }
    }
    int index = 0;
    for (const CallButton &button : calls) {
      const ImVec2 pos = at[n++];
      const std::string id = "call" + std::to_string(index++);
      const Icon glyph = button.readback ? Icon::check
                         : button.choice ? Icon::arrow_down
                                         : Icon::menu;
      if (call_pill(draw, p, id.c_str(), pos, button.width, button.label, glyph,
                    button.readback, !button.in_flight, everything)) {
        g.state->pressed_call = button.key;
      }
      if (button.open) {
        draw->AddRect(pos, ImVec2(pos.x + button.width, pos.y + kCallHeight), rgba(p.accent),
                      kCallHeight * 0.5f, 0, 2.0f);
      }
    }
    *widest = std::max(*widest, strip_w);
    g.state->strip_width = strip_w;
    return strip_tail(draw, session, p, right, y + strip_h, strip_w, widest);
  }

  const float strip_h = kCallHeight + pad * 2.0f;
  const float strip_w = std::min(most, fixed_w + calls_w + pad + strip_h * 0.5f);
  y += kGap;
  const ImVec2 min(right - strip_w, y);
  const ImVec2 max(right, y + strip_h);
  draw->AddRectFilled(min, max, glass(p), strip_h * 0.5f);
  draw->AddRect(min, max, edge(p), strip_h * 0.5f, 1.0f);

  const float row = y + pad;
  float x = min.x + pad;
  const ImVec4 everything(min.x, min.y, max.x, max.y);
  if (has_next) {
    if (call_pill(draw, p, "next_frequency", ImVec2(x, row), pill_width(next_label, true),
                  next_label, Icon::arrow_down, false, true, everything)) {
      g.state->pressed_next = true;
    }
    x += pill_width(next_label, true) + kCallGap;
  }

  // The calls, scrolled and clipped to what is left of the strip. They stop
  // short of the strip's rounded end, so one cut off by the edge is cut by
  // glass rather than by the curve.
  float left = x;
  float end = max.x - strip_h * 0.5f;
  // More calls than fit: a button at each end that scrolls, because a wheel
  // is not something every pilot has (a trackpad in the sim sends none the
  // HUD sees), and a strip that only scrolls under one is a strip whose last
  // calls cannot be reached.
  const bool overflowing = calls_w > end - left;
  constexpr float kArrow = 24.0f;
  ImVec2 back_at, ahead_at;
  if (overflowing) {
    back_at = ImVec2(left, row);
    left += kArrow + kCallGap;
    ahead_at = ImVec2(max.x - pad - kArrow, row);
    end = ahead_at.x - kCallGap;
  }
  const ImVec4 clip(left, min.y, end, max.y);
  const float visible = clip.z - clip.x;
  const float overflow = std::max(0.0f, calls_w - visible);
  ImGuiIO &io = ImGui::GetIO();
  if (overflow > 0.0f && ImGui::IsMouseHoveringRect(min, max, false)) {
    g.state->strip_scroll -= (io.MouseWheel + io.MouseWheelH) * 40.0f;
  }
  if (overflowing) {
    const float page = std::max(40.0f, visible * 0.8f);
    const bool can_back = g.state->strip_scroll > 0.5f;
    const bool can_ahead = g.state->strip_scroll < overflow - 0.5f;
    if (scroll_arrow(draw, p, "strip_back", back_at, kArrow, false, can_back)) {
      g.state->strip_scroll -= page;
    }
    if (scroll_arrow(draw, p, "strip_ahead", ahead_at, kArrow, true, can_ahead)) {
      g.state->strip_scroll += page;
    }
  }
  g.state->strip_scroll = std::clamp(g.state->strip_scroll, 0.0f, overflow);
  x = left;
  draw->PushClipRect(ImVec2(clip.x, clip.y), ImVec2(clip.z, clip.w), true);
  float cx = x - g.state->strip_scroll;
  int index = 0;
  for (const CallButton &button : calls) {
    const std::string id = "call" + std::to_string(index++);
    const Icon glyph = button.readback ? Icon::check
                       : button.choice ? Icon::arrow_down
                                       : Icon::menu;
    if (call_pill(draw, p, id.c_str(), ImVec2(cx, row), button.width, button.label, glyph,
                  button.readback, !button.in_flight, clip)) {
      g.state->pressed_call = button.key;
    }
    if (button.open) {
      // Open, its options below: ringed rather than filled, because filled
      // is the readback's.
      draw->AddRect(ImVec2(cx, row), ImVec2(cx + button.width, row + kCallHeight),
                    rgba(p.accent), kCallHeight * 0.5f, 0, 2.0f);
    }
    cx += button.width + kCallGap;
  }
  draw->PopClipRect();
  if (overflow > 0.0f) {
    // A fade at whichever edge has more, so a strip that scrolls says so.
    const ImU32 solid = rgba(p.sunken, 0.6f);
    const ImU32 clear = rgba(p.sunken, 0.0f);
    if (g.state->strip_scroll < overflow) {
      draw->AddRectFilledMultiColor(ImVec2(clip.z - 18.0f, row), ImVec2(clip.z, row + kCallHeight),
                                    clear, solid, solid, clear);
    }
    if (g.state->strip_scroll > 0.0f) {
      draw->AddRectFilledMultiColor(ImVec2(clip.x, row), ImVec2(clip.x + 18.0f, row + kCallHeight),
                                    solid, clear, clear, solid);
    }
  }
  *widest = std::max(*widest, strip_w);
  g.state->strip_width = strip_w;
  y += strip_h;

  return strip_tail(draw, session, p, right, y, strip_w, widest);
}

// --- the typing line ---------------------------------------------------------
//
// A pilot who would rather type than speak or press: Enter sends what they
// wrote as a typed call, the way the panel's box does, without opening the
// panel. As wide as the call strip under it, or the bar where there is none.

constexpr float kTypeHeight = 28.0f;

float typing_line(ImDrawList *draw, const Palette &p, float right, float y, float bar_width,
                  float *widest, bool clear) {
  const float most = gui::HudPlacement{}.width - kPad * 2.0f;
  const float width =
      std::clamp(std::max(g.state->strip_width, bar_width), 240.0f, most);
  y += kGap;
  const ImVec2 min(right - width, y);
  const ImVec2 max(right, y + kTypeHeight);
  draw->AddRectFilled(min, max, glass(p), kTypeHeight * 0.5f);
  draw->AddRect(min, max, edge(p), kTypeHeight * 0.5f, 1.0f);

  const float px = css(kActionPx + 1.0f);
  ImGui::PushFont(g.face->regular, px);
  ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_Text, rgba(p.text));
  ImGui::PushStyleColor(ImGuiCol_TextDisabled, rgba(p.text_dim));
  ImGui::PushStyleColor(ImGuiCol_InputTextCursor, rgba(p.accent));
  ImGui::PushStyleColor(ImGuiCol_TextSelectedBg, rgba(p.accent, 0.35f));
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, (kTypeHeight - px) * 0.5f));
  ImGui::SetCursorScreenPos(ImVec2(min.x + 14.0f, min.y));
  ImGui::SetNextItemWidth(width - 28.0f);
  if (ImGui::InputTextWithHint("##hud_typed", tr("Type a call, Enter to send"),
                               g.state->typed, sizeof(g.state->typed),
                               ImGuiInputTextFlags_EnterReturnsTrue) &&
      clear) {
    // Over a voice the line is kept, to be sent once RX goes out
    // (x-z7a/atc#1143).
    g.state->pressed_text = g.state->typed;
    g.state->typed[0] = '\0';
  }
  g.state->typing_active = ImGui::IsItemActive();
  ImGui::PopStyleVar();
  ImGui::PopStyleColor(7);
  ImGui::PopFont();
  *widest = std::max(*widest, width);
  return y + kTypeHeight;
}

// --- the flight plan notice ---------------------------------------------------
//
// What the console just filed, for kHudPlanNoticeMs: where the plan says the
// pilot is going. The plugin reloads the plan on its own when the aircraft
// loads, so this is often the only sign one arrived. Clicked, it goes.

constexpr float kNoticePad = 10.0f;
constexpr float kNoticeRoutePx = 15.0f;
constexpr float kNoticeLine = 17.0f;

float plan_notice(ImDrawList *draw, HudSession &session, const Palette &p,
                  float right, float y, float *widest) {
  const gui::HudPlanNotice *notice = session.plan_notice();
  if (notice == nullptr) {
    return y;
  }
  using Kind = gui::HudPlanNotice::Kind;
  const bool failed = notice->kind == Kind::failed;
  const unsigned ink = failed ? p.danger : p.active;
  const std::string heading = failed ? tr("No flight plan loaded")
                              : notice->kind == Kind::new_flight
                                  ? tr("New flight: plan received")
                                  : tr("Flight plan received");
  const float room = kErrorMaxWidth - kNoticePad * 2.0f;
  ImFont *semibold = g.face->semibold;
  ImFont *bold = g.face->bold;
  ImFont *regular = g.face->regular;

  std::vector<std::string> lines;
  if (failed) {
    lines.push_back(fit(regular, css(kActionPx), notice->error, room));
  } else {
    if (!notice->detail.empty()) {
      lines.push_back(fit(regular, css(kActionPx), notice->detail, room));
    }
    if (!notice->route.empty()) {
      lines.push_back(fit(regular, css(kActionPx), tr("via ") + notice->route, room));
    }
  }
  const std::string origin = notice->origin.empty() ? "?" : notice->origin;
  const float arrow = 18.0f;
  float width = kIcon + 6.0f + measure(semibold, css(kActionPx), heading).x;
  if (!failed) {
    width = std::max(width, measure(bold, css(kNoticeRoutePx), origin).x + arrow +
                                measure(bold, css(kNoticeRoutePx), notice->destination).x);
  }
  for (const std::string &line : lines) {
    width = std::max(width, measure(regular, css(kActionPx), line).x);
  }
  width = std::min(width, room) + kNoticePad * 2.0f;
  const float height = kNoticePad * 2.0f + kNoticeLine * (failed ? 1.0f : 2.0f) +
                       kNoticeLine * static_cast<float>(lines.size());

  y += kGap;
  const ImVec2 min(right - width, y);
  const ImVec2 max(right, y + height);
  bool hovered = false;
  if (hit("plan_notice", min, ImVec2(width, height), &hovered)) {
    session.dismiss_plan_notice();
  }
  draw->AddRectFilled(min, max, glass(p), kBarRadius);
  draw->AddRect(min, max, hovered ? rgba(ink, 0.9f) : rgba(ink, 0.5f), kBarRadius, 1.0f);
  draw->AddRectFilled(min, ImVec2(min.x + 3.0f, max.y), rgba(ink), kBarRadius,
                      ImDrawFlags_RoundCornersLeft);

  float ty = min.y + kNoticePad;
  float tx = min.x + kNoticePad;
  icon(draw, failed ? Icon::warn : Icon::check,
       ImVec2(tx, ty + (kNoticeLine - kIcon) * 0.5f), kIcon, rgba(ink));
  text(draw, semibold, css(kActionPx),
       ImVec2(tx + kIcon + 6.0f, ty + (kNoticeLine - css(kActionPx)) * 0.5f), rgba(ink),
       heading, p);
  ty += kNoticeLine;
  if (!failed) {
    const float ry = ty + (kNoticeLine - css(kNoticeRoutePx)) * 0.5f + 2.0f;
    text(draw, bold, css(kNoticeRoutePx), ImVec2(tx, ry), rgba(p.text), origin, p);
    const float ax = tx + measure(bold, css(kNoticeRoutePx), origin).x + 4.0f;
    const float am = ty + kNoticeLine * 0.5f + 2.0f;
    draw->AddLine(ImVec2(ax, am), ImVec2(ax + arrow - 8.0f, am), rgba(p.text_dim), 1.5f);
    draw->AddTriangleFilled(ImVec2(ax + arrow - 8.0f, am - 3.5f),
                            ImVec2(ax + arrow - 8.0f, am + 3.5f),
                            ImVec2(ax + arrow - 3.0f, am), rgba(p.text_dim));
    text(draw, bold, css(kNoticeRoutePx), ImVec2(ax + arrow, ry), rgba(p.text),
         notice->destination, p);
    ty += kNoticeLine;
  }
  for (const std::string &line : lines) {
    text(draw, regular, css(kActionPx),
         ImVec2(tx, ty + (kNoticeLine - css(kActionPx)) * 0.5f), rgba(p.text_dim), line, p);
    ty += kNoticeLine;
  }
  *widest = std::max(*widest, width);
  return max.y;
}

// --- one frame ---------------------------------------------------------------

// Ends the frame begun by build, recording how big it was and whether the
// mouse is over it. The window is exactly as big as the HUD, so a mouse
// anywhere in it is over the HUD.
void finish(float widest, float y) {
  const ImGuiIO &io = ImGui::GetIO();
  g.state->hovered = ImGui::IsMousePosValid() && io.MousePos.x >= 0.0f &&
                     io.MousePos.y >= 0.0f && io.MousePos.x < io.DisplaySize.x &&
                     io.MousePos.y < io.DisplaySize.y;
  ImGui::End();
  ImGui::PopStyleVar(2);

  g.state->content_width = widest + kPad * 2.0f;
  g.state->content_height = y + 4.0f;
}

void build(HudSession &session, std::int64_t wall, bool collapsed) {
  const Palette &p = palette(session.theme());
  ImFont *regular = g.face->regular;
  ImFont *semibold = g.face->semibold;
  ImFont *bold = g.face->bold;

  ImGuiIO &io = ImGui::GetIO();
  ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
  ImGui::SetNextWindowSize(io.DisplaySize);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  ImGui::Begin("zoal-atc HUD", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoScrollWithMouse |
                   ImGuiWindowFlags_NoFocusOnAppearing |
                   ImGuiWindowFlags_NoBringToFrontOnFocus);
  ImDrawList *draw = ImGui::GetWindowDrawList();
  g.state->typing_active = false;

  // Everything hangs from the window's right edge, so the HUD hugs the
  // screen's corner and grows leftward.
  const float right = io.DisplaySize.x - kPad;
  const float top = kPad;
  const auto &menu = session.menu();
  const bool open = menu.open();
  const bool reachable = session.reachable();
  float widest = 0.0f;

  // ---- the bar ----
  const std::string title =
      session.callsign().empty() ? "zoal-atc" : session.callsign();
  const std::string warn = tr("CONSOLE UNREACHABLE");
  // The flight number stays on the bar with the menu open: it is what the
  // pilot reads to know whose calls the buttons make.
  float bar_width = 11.0f + kLed + 8.0f + kHandle + 3.0f;
  bar_width += tracked_width(bold, css(kTitlePx), title, kTitleTracking) + 8.0f;
  // The radio's annunciators, as a COM radio shows them (x-z7a/atc#1143):
  // RX while a voice is on the channel the pilot transmits on, TX while the
  // microphone is open. Dim when out.
  const float annunciator = tracked_width(bold, css(kWarnPx), "RX", kWarnTracking);
  bar_width += (annunciator + 6.0f) * 2.0f + 2.0f;
  if (!open && !reachable) {
    bar_width += tracked_width(bold, css(kWarnPx), warn, kWarnTracking) + 8.0f;
  }
  const ImVec2 bar_min(right - bar_width, top);
  const ImVec2 bar_max(right, top + kBarHeight);
  draw->AddRectFilled(bar_min, bar_max, glass(p), kBarRadius);
  draw->AddRect(bar_min, bar_max, edge(p), kBarRadius, 1.0f);
  const float mid = top + kBarHeight * 0.5f;
  float x = bar_min.x + 11.0f;
  const unsigned led = reachable ? p.active : p.warn;
  draw->AddCircleFilled(ImVec2(x + kLed * 0.5f, mid), kLed * 1.1f,
                        rgba(led, 0.25f));
  draw->AddCircleFilled(ImVec2(x + kLed * 0.5f, mid), kLed * 0.5f, rgba(led));
  x += kLed + 8.0f;
  tracked_text(draw, bold, css(kTitlePx), ImVec2(x, mid - css(kTitlePx) * 0.5f),
               rgba(p.text), title, p, kTitleTracking);
  x += tracked_width(bold, css(kTitlePx), title, kTitleTracking) + 8.0f;
  for (const auto &[label, lit, ink] :
       {std::tuple<const char *, bool, unsigned>{"RX", session.receiving(), p.active},
        std::tuple<const char *, bool, unsigned>{"TX", session.transmitting(), p.danger}}) {
    tracked_text(draw, bold, css(kWarnPx), ImVec2(x, mid - css(kWarnPx) * 0.5f),
                 lit ? rgba(ink) : rgba(p.text_dim, 0.45f), label, p, kWarnTracking);
    x += annunciator + 6.0f;
  }
  x += 2.0f;
  if (!open) {
    if (!reachable) {
      tracked_text(draw, bold, css(kWarnPx), ImVec2(x, mid - css(kWarnPx) * 0.5f),
                   rgba(p.warn), warn, p, kWarnTracking);
      x += tracked_width(bold, css(kWarnPx), warn, kWarnTracking) + 8.0f;
    }
  }
  {
    // The bar, all but its handle, is what the HUD is dragged by: held, the
    // window follows the mouse. A press that does not move is no drag.
    bool hovered = false;
    hit("drag", bar_min, ImVec2(std::max(1.0f, x - bar_min.x), kBarHeight), &hovered);
    g.state->dragging = ImGui::IsItemActive();
    if (hovered || g.state->dragging) {
      ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    }
  }
  {
    const ImVec2 handle_min(x, mid - kHandle * 0.5f);
    bool hovered = false;
    if (hit("handle", handle_min, ImVec2(kHandle, kHandle), &hovered)) {
      session.toggle_menu();
    }
    const ImVec2 centre(x + kHandle * 0.5f, mid);
    if (hovered || open) {
      draw->AddCircleFilled(centre, kHandle * 0.5f, rgba(p.accent, 0.16f));
    }
    draw->AddCircle(centre, kHandle * 0.5f, edge(p), 0, 1.0f);
    const float size = 14.0f;
    icon(draw, open ? Icon::close : Icon::menu,
         ImVec2(centre.x - size * 0.5f, centre.y - size * 0.5f), size,
         rgba(p.accent));
  }
  float row_width = bar_width;

  // ---- the menu, in the same row, so opening it never changes the height ----
  if (open && !menu.error().empty()) {
    const float text_room = kErrorMaxWidth - 10.0f - kIcon - 6.0f - 6.0f -
                            kHandle - 3.0f;
    const std::string shown = fit(semibold, css(kActionPx), menu.error(), text_room);
    const float width = 10.0f + kIcon + 6.0f +
                        measure(semibold, css(kActionPx), shown).x + 6.0f + kHandle +
                        3.0f;
    const ImVec2 min(bar_min.x - kGap - width, top);
    const ImVec2 max(bar_min.x - kGap, top + kBarHeight);
    draw->AddRectFilled(min, max, glass(p), kBarRadius);
    draw->AddRect(min, max, rgba(p.danger, 0.5f), kBarRadius, 1.0f);
    float ex = min.x + 10.0f;
    icon(draw, Icon::warn, ImVec2(ex, mid - kIcon * 0.5f), kIcon,
         rgba(p.danger));
    ex += kIcon + 6.0f;
    text(draw, semibold, css(kActionPx), ImVec2(ex, mid - css(kActionPx) * 0.5f),
         rgba(p.danger), shown, p);
    const ImVec2 dismiss_min(max.x - 3.0f - kHandle, mid - kHandle * 0.5f);
    bool hovered = false;
    if (hit("dismiss", dismiss_min, ImVec2(kHandle, kHandle), &hovered)) {
      session.dismiss_error();
    }
    if (hovered) {
      draw->AddCircleFilled(
          ImVec2(dismiss_min.x + kHandle * 0.5f, mid), kHandle * 0.5f,
          rgba(p.danger, 0.16f));
    }
    const float size = 13.0f;
    icon(draw, Icon::close,
         ImVec2(dismiss_min.x + (kHandle - size) * 0.5f, mid - size * 0.5f),
         size, rgba(p.danger));
    row_width += kGap + width;
  } else if (open) {
    std::vector<ActionButton> buttons = action_buttons(session);
    float width = 2.0f * 2.0f;
    for (const ActionButton &button : buttons) {
      width += button.width;
    }
    width += 2.0f * static_cast<float>(buttons.size() - 1);
    const ImVec2 min(bar_min.x - kGap - width, top);
    const ImVec2 max(bar_min.x - kGap, top + kBarHeight);
    draw->AddRectFilled(min, max, glass(p), kBarRadius);
    draw->AddRect(min, max, edge(p), kBarRadius, 1.0f);
    float bx = min.x + 2.0f;
    for (const ActionButton &button : buttons) {
      const ImVec2 bmin(bx, mid - kActionHeight * 0.5f);
      const ImVec2 bmax(bx + button.width, mid + kActionHeight * 0.5f);
      bool hovered = false;
      const bool clicked =
          hit(button.id, bmin, ImVec2(button.width, kActionHeight), &hovered);
      if (clicked && button.enabled) {
        const std::string id = button.id;
        g.state->pressed = id == "reload"       ? HudAction::reload_plan
                         : id == "new_flight" ? HudAction::end_flight
                                              : HudAction::open_panel;
      }
      const float alpha = button.enabled ? 1.0f : 0.45f;
      ImU32 fg = rgba(p.text);
      ImU32 icon_colour = rgba(p.accent);
      if (button.armed) {
        // Armed is a warning colour: the next press throws a clearance away.
        draw->AddRectFilled(bmin, bmax, rgba(p.warn, 0.85f), kActionHeight * 0.5f);
        fg = rgba(p.on_warn);
        icon_colour = fg;
      } else if (hovered && button.enabled) {
        draw->AddRectFilled(bmin, bmax, rgba(p.accent, 0.16f),
                            kActionHeight * 0.5f);
        draw->AddRect(bmin, bmax, edge(p), kActionHeight * 0.5f, 1.0f);
      }
      if (button.done) {
        fg = rgba(p.active);
        icon_colour = fg;
      }
      icon(draw, button.icon, ImVec2(bx + 8.0f, mid - kIcon * 0.5f), kIcon,
           faded(icon_colour, alpha));
      const float tx = bx + 8.0f + kIcon + 5.0f;
      if (button.armed) {
        draw->AddText(semibold, css(kActionPx), ImVec2(tx, mid - css(kActionPx) * 0.5f),
                      faded(fg, alpha), gui::tr(button.label).c_str());
      } else {
        text(draw, semibold, css(kActionPx), ImVec2(tx, mid - css(kActionPx) * 0.5f), fg,
             button.label, p, alpha);
      }
      bx += button.width + 2.0f;
    }
    row_width += kGap + width;
  }
  widest = std::max(widest, row_width);
  float y = top + kBarHeight;

  // ---- the flight plan the console just filed ----
  y = plan_notice(draw, session, p, right, y, &widest);
  // A minimized HUD at rest is its bar and nothing under it.
  if (collapsed) {
    finish(widest, y);
    return;
  }

  // ---- a line to type a call in, as wide as the strip under it ----
  if (session.shows_typing()) {
    y = typing_line(draw, p, right, y, row_width, &widest, session.may_key_up());
  }

  // ---- the calls a pilot can make by pressing a button ----
  y = call_strip(draw, session, p, right, y, &widest);

  // ---- the transmissions ----
  const float bubble_max = gui::HudPlacement{}.width - kPad * 2.0f;
  const float wrap = bubble_max - kBubblePadX * 2.0f;
  static const std::vector<HudLine> kNone;
  for (const HudLine &line : session.shows_transcript() ? session.feed().lines() : kNone) {
    const float opacity = gui::hud_line_opacity(line, wall);
    if (opacity <= 0.0f) {
      continue;
    }
    const gui::HudEntry &entry = line.entry;
    const bool from_pilot = entry.kind != "atc";
    const HudTone tone = gui::hud_tone(entry);
    const unsigned seat = seat_colour(p, tone);
    const std::string speaker = upper(gui::tr(gui::hud_speaker(entry, session.callsign())));
    const std::string tag =
        entry.overheard
            ? (entry.addressed_to.empty() ? "overheard" : tr("to ") + entry.addressed_to)
            : "";

    const std::vector<std::string> lines =
        wrap_lines(regular, css(kBubblePx), gui::display_numbers(entry.text), wrap);
    float words_w = 0.0f;
    for (const std::string &part : lines) {
      words_w = std::max(words_w, measure(regular, css(kBubblePx), part).x);
    }
    const float bubble_w = std::min(bubble_max, words_w + kBubblePadX * 2.0f);
    const float bubble_h =
        kBubbleLine * static_cast<float>(lines.size()) + kBubblePadY * 2.0f;
    const float speaker_w =
        tracked_width(bold, css(kSpeakerPx), speaker, kSpeakerTracking);
    float label_w = speaker_w + 8.0f;
    const float tag_w =
        tag.empty() ? 0.0f : measure(regular, css(kTagPx), tag).x + 14.0f;
    if (!tag.empty()) {
      label_w += 6.0f + tag_w;
    }
    const float row_w = std::max(bubble_w, label_w);
    const float label_h = 16.0f;

    y += kGap;
    const float row_left = right - row_w;
    // A controller's name sits on the left of its row, this cockpit's on the
    // right, so whose words they were reads before the words do.
    float lx = from_pilot ? right - label_w + 4.0f : row_left + 4.0f;
    tracked_text(draw, bold, css(kSpeakerPx),
                 ImVec2(lx, y + (label_h - css(kSpeakerPx)) * 0.5f), rgba(seat),
                 speaker, p, kSpeakerTracking, opacity);
    if (!tag.empty()) {
      lx += speaker_w + 6.0f;
      const ImVec2 tmin(lx, y);
      const ImVec2 tmax(lx + tag_w, y + label_h);
      draw->AddRectFilled(tmin, tmax, faded(rgba(p.raised), opacity),
                          label_h * 0.5f);
      draw->AddText(regular, css(kTagPx),
                    ImVec2(lx + 7.0f, y + (label_h - css(kTagPx)) * 0.5f),
                    faded(rgba(p.text_dim), opacity), tag.c_str());
    }
    y += label_h + kSpeakerGap;

    const float bubble_left = from_pilot ? right - bubble_w : row_left;
    const ImVec2 bmin(bubble_left, y);
    const ImVec2 bmax(bubble_left + bubble_w, y + bubble_h);
    // The speaker's corner is tucked in, as in the panel's radio log.
    const ImDrawFlags corners =
        ImDrawFlags_RoundCornersAll &
        ~(from_pilot ? ImDrawFlags_RoundCornersTopRight
                     : ImDrawFlags_RoundCornersTopLeft);
    ImU32 fill = glass(p);
    ImU32 border = rgba(seat, 0.30f);
    ImU32 words_colour = rgba(p.text);
    if (entry.overheard) {
      fill = rgba(p.sunken, 0.45f);
      border = rgba(p.border, p.border_alpha);
      words_colour = rgba(p.text_feed);
    } else if (from_pilot) {
      // The panel's sage wash would vanish over the sky; mixed into the glass.
      fill = rgba(blend(p.sunken, p.active, 0.21f), 0.77f);
      border = rgba(p.active, 0.26f);
    }
    draw->AddRectFilled(bmin, bmax, faded(fill, opacity), kBubbleRadius, corners);
    draw->AddRect(bmin, bmax, faded(border, opacity), kBubbleRadius, 1.0f,
                  corners);
    float ly = bmin.y + kBubblePadY + (kBubbleLine - css(kBubblePx)) * 0.5f;
    for (const std::string &part : lines) {
      text(draw, regular, css(kBubblePx), ImVec2(bmin.x + kBubblePadX, ly),
           words_colour, part, p, opacity);
      ly += kBubbleLine;
    }
    y += bubble_h;
    widest = std::max(widest, row_w);
  }

  finish(widest, y);
}


} // namespace

void draw_hud(gui::HudSession &session, HudViewState &state, const Typeface &face,
              const HudFrame &frame) {
  g = Frame{&face, &state};
  build(session, frame.wall_ms, frame.collapsed);
  g = Frame{};
}

} // namespace zoal_atc::ui
