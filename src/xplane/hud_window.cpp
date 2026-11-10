#include "hud_window.hpp"

#include "imgui_window.hpp"
#include "zoal_atc/gui/hud_layout.hpp"
#include "zoal_atc/gui/hud_session.hpp"
#include "zoal_atc/gui/panel_paths.hpp"
#include "../ui/kit.hpp"

#include <XPLMDataAccess.h>
#include <XPLMDisplay.h>
#include <XPLMPlugin.h>
#include <XPLMUtilities.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "imgui.h"

namespace zoal_atc::xplane {

namespace {

using gui::HudAction;
using gui::HudLine;
using gui::HudSession;
using gui::HudTheme;
using gui::HudTone;

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

// The roles the HUD uses from tokens.css, per theme.
struct Palette {
  unsigned sunken, text, text_dim, text_feed, accent, active, warn, on_warn,
      danger, delivery, seat, tower, raised, border;
  float border_alpha;
  unsigned shadow;
  float shadow_alpha;
};

constexpr Palette kGraphite{0x121213, 0xefebe5, 0xa6a099, 0xd8d2ca, 0xefb45c,
                            0xa9d39f, 0xff9b6a, 0x2b1206, 0xff8a7a, 0xc4b3e6,
                            0xe8a989, 0x9fc4e8, 0x2d2d30, 0xfff8f0, 0.15f,
                            0x000000, 0.70f};
constexpr Palette kDaylight{0xece6dd, 0x27231e, 0x6b6358, 0x3d3731, 0xa35d08,
                            0x3b7738, 0xc0561d, 0xffffff, 0xb93a2a, 0x6d53a6,
                            0xad552a, 0x2d6a9b, 0xe8e2d8, 0x463219, 0.21f,
                            0xffffff, 0.55f};
constexpr Palette kNight{0x090807, 0xc8b8a7, 0x85776a, 0xb4a493, 0xcc8748,
                         0x8cab81, 0xd8844f, 0x1f0c03, 0xd4705f, 0xa394c0,
                         0xc48b70, 0x8aa5c0, 0x1f1b18, 0xffebd7, 0.12f,
                         0x000000, 0.70f};

const Palette &palette(HudTheme theme) {
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

// One tint for all of it, so the HUD reads as one instrument.
ImU32 glass(const Palette &p) { return rgba(p.sunken, 0.72f); }
ImU32 edge(const Palette &p) { return rgba(p.accent, 0.22f); }

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

// --- state -------------------------------------------------------------------

struct Hud {
  std::unique_ptr<ImguiWindow> window;
  std::unique_ptr<HudSession> session;
  std::function<std::uint64_t()> steady_ms;
  // Inter, or Roboto without it; the panel is set in the same.
  ui::Typeface face;
  XPLMCommandRef command = nullptr;
  XPLMDataRef ui_scale = nullptr;
  // A button pressed while drawing, run from the flight loop: an action may
  // open the panel, and X-Plane's windows are not rearranged mid-draw.
  HudAction pressed = HudAction::none;
  // What the last frame drew, so the window can be made exactly that big.
  float content_width = 0.0f;
  float content_height = kBarHeight + kPad * 2.0f;
};

std::unique_ptr<Hud> g_hud;

// A CSS px size as an ImGui font size, for the face the HUD is set in.
float css(float px) { return g_hud->face.css(px); }

void hud_log(const std::string &message) {
  XPLMDebugString(("[zoal-atc hud] " + message + "\n").c_str());
}

std::uint64_t steady_now() { return g_hud->steady_ms ? g_hud->steady_ms() : 0; }

std::int64_t wall_now() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
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
  return ui::tracked_width(g_hud->face, font, size, value, tracking);
}

void tracked_text(ImDrawList *draw, ImFont *font, float size, ImVec2 pos,
                  ImU32 colour, const std::string &value, const Palette &p,
                  float tracking, float opacity = 1.0f) {
  ui::tracked_text(draw, g_hud->face, font, size, pos, faded(colour, opacity),
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
                     reloaded ? "Plan reloaded" : "Reload plan",
                     menu.reload_enabled(), false, reloaded});
  const bool started = menu.done() == HudAction::end_flight;
  buttons.push_back({"new_flight",
                     menu.armed() ? Icon::warn
                                  : (started ? Icon::check : Icon::plane),
                     menu.armed() ? "Confirm"
                                  : (started ? "Started" : "New flight"),
                     menu.new_flight_enabled(), menu.armed(), started});
  buttons.push_back({"open_panel", Icon::panel, "Open panel",
                     menu.open_panel_enabled(), false, false});
  for (ActionButton &button : buttons) {
    button.width = 8.0f + kIcon + 5.0f +
                   measure(g_hud->face.semibold, css(kActionPx), button.label).x + 10.0f;
  }
  return buttons;
}

// --- one frame ---------------------------------------------------------------

void build() {
  HudSession &session = *g_hud->session;
  const Palette &p = palette(session.theme());
  ImFont *regular = g_hud->face.regular;
  ImFont *semibold = g_hud->face.semibold;
  ImFont *bold = g_hud->face.bold;
  const std::int64_t wall = wall_now();

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
  const std::string warn = "CONSOLE UNREACHABLE";
  float bar_width = 11.0f + kLed + 8.0f + kHandle + 3.0f;
  if (!open) {
    bar_width += tracked_width(bold, css(kTitlePx), title, kTitleTracking) + 8.0f;
    if (!reachable) {
      bar_width += tracked_width(bold, css(kWarnPx), warn, kWarnTracking) + 8.0f;
    }
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
  if (!open) {
    tracked_text(draw, bold, css(kTitlePx), ImVec2(x, mid - css(kTitlePx) * 0.5f),
                 rgba(p.text), title, p, kTitleTracking);
    x += tracked_width(bold, css(kTitlePx), title, kTitleTracking) + 8.0f;
    if (!reachable) {
      tracked_text(draw, bold, css(kWarnPx), ImVec2(x, mid - css(kWarnPx) * 0.5f),
                   rgba(p.warn), warn, p, kWarnTracking);
      x += tracked_width(bold, css(kWarnPx), warn, kWarnTracking) + 8.0f;
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
        g_hud->pressed = id == "reload"       ? HudAction::reload_plan
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
                      faded(fg, alpha), button.label.c_str());
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

  // ---- the transmissions ----
  const float bubble_max = gui::HudPlacement{}.width - kPad * 2.0f;
  const float wrap = bubble_max - kBubblePadX * 2.0f;
  for (const HudLine &line : session.feed().lines()) {
    const float opacity = gui::hud_line_opacity(line, wall);
    if (opacity <= 0.0f) {
      continue;
    }
    const gui::HudEntry &entry = line.entry;
    const bool from_pilot = entry.kind != "atc";
    const HudTone tone = gui::hud_tone(entry);
    const unsigned seat = seat_colour(p, tone);
    const std::string speaker = upper(gui::hud_speaker(entry, session.callsign()));
    const std::string tag =
        entry.overheard
            ? (entry.addressed_to.empty() ? "overheard" : "to " + entry.addressed_to)
            : "";

    const std::vector<std::string> lines =
        wrap_lines(regular, css(kBubblePx), entry.text, wrap);
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

  ImGui::End();
  ImGui::PopStyleVar(2);

  g_hud->content_width = widest + kPad * 2.0f;
  g_hud->content_height = y + 4.0f;
}

// --- window ------------------------------------------------------------------

void place() {
  gui::ScreenRect screen;
  XPLMGetScreenBoundsGlobal(&screen.left, &screen.top, &screen.right,
                            &screen.bottom);
  gui::HudPlacement placement;
  placement.width = static_cast<int>(std::ceil(
      std::clamp(g_hud->content_width, 1.0f, static_cast<float>(placement.width))));
  const gui::ScreenRect rect = gui::hud_rect(
      screen, static_cast<int>(std::ceil(g_hud->content_height)), placement);
  g_hud->window->set_geometry(rect.left, rect.top, rect.right, rect.bottom);
}

void set_shown(bool shown) {
  if (shown) {
    place();
  }
  g_hud->window->set_visible(shown);
  g_hud->session->set_visible(shown, steady_now());
}

int toggle_handler(XPLMCommandRef, XPLMCommandPhase phase, void *) {
  if (phase == xplm_CommandBegin && g_hud) {
    set_shown(!g_hud->window->visible());
  }
  return 1;
}

std::filesystem::path plugin_fonts_dir() {
  char binary[1024] = {};
  XPLMGetPluginInfo(XPLMGetMyID(), nullptr, binary, nullptr, nullptr);
  return std::filesystem::path(gui::plugin_root_from_binary_path(binary)) /
         "fonts";
}

// Inter, shipped in the plugin's fonts/ folder; without it, X-Plane's own UI
// face, Roboto, which every install has.
void load_fonts() {
  char system_path[1024] = {};
  XPLMGetSystemPath(system_path);
  g_hud->face = ui::load_typeface(
      plugin_fonts_dir(),
      std::filesystem::path(system_path) / "Resources" / "fonts", hud_log);
}

} // namespace

void start_hud(gui::GuiBridge &bridge,
               std::function<std::uint64_t()> steady_ms) {
  if (g_hud) {
    return;
  }
  g_hud = std::make_unique<Hud>();
  g_hud->steady_ms = std::move(steady_ms);
  g_hud->window = std::make_unique<ImguiWindow>(0, 40, 440, 0, build);
  if (!g_hud->window->valid()) {
    hud_log("X-Plane did not create the HUD window (it needs X-Plane 12.4.4 "
            "or newer); the panel still works");
    g_hud.reset();
    return;
  }
  {
    ImGuiContext *previous = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(g_hud->window->context());
    load_fonts();
    ImGuiStyle &style = ImGui::GetStyle();
    style.ItemSpacing = ImVec2(0.0f, 0.0f);
    ImGui::SetCurrentContext(previous);
  }
  g_hud->ui_scale = XPLMFindDataRef("sim/graphics/misc/user_interface_scale");
  g_hud->session = std::make_unique<HudSession>(bridge);
  g_hud->session->attach(steady_now());

  g_hud->command =
      XPLMCreateCommand(kHudCommandName, "zoal-atc: Toggle the radio HUD");
  XPLMRegisterCommandHandler(g_hud->command, toggle_handler, 1, nullptr);
  // Up from the start: the HUD is how ATC is heard with the panel closed,
  // which is most of a flight.
  set_shown(true);
  hud_log("up; toggle with Plugins > zoal-atc > Toggle HUD or " +
          std::string(kHudCommandName));
}

void stop_hud() {
  if (!g_hud) {
    return;
  }
  if (g_hud->command) {
    XPLMUnregisterCommandHandler(g_hud->command, toggle_handler, 1, nullptr);
  }
  g_hud->session->detach();
  g_hud->window.reset();
  g_hud.reset();
}

void tick_hud() {
  if (!g_hud) {
    return;
  }
  const std::uint64_t steady = steady_now();
  const std::int64_t wall = wall_now();
  HudSession &session = *g_hud->session;
  switch (std::exchange(g_hud->pressed, HudAction::none)) {
  case HudAction::reload_plan:
    session.press_reload(steady, wall);
    break;
  case HudAction::end_flight:
    session.press_new_flight(steady, wall);
    break;
  case HudAction::open_panel:
    session.press_open_panel(steady);
    break;
  case HudAction::none:
    break;
  }
  session.pump(steady, wall);
  // Text is rasterized for the screen's real density: X-Plane reports its UI
  // scale but not the display's (a Retina screen draws two pixels a boxel and
  // says 1:1), so twice the UI scale keeps it sharp on both kinds of screen.
  float scale = 1.0f;
  if (g_hud->ui_scale) {
    scale = std::max(1.0f, XPLMGetDataf(g_hud->ui_scale));
  }
  g_hud->window->set_framebuffer_scale(2.0f * scale);
  if (g_hud->window->visible()) {
    place();
  }
}

void hud_settings_changed(const std::string &payload_json) {
  if (g_hud) {
    g_hud->session->on_settings_changed(payload_json);
  }
}

} // namespace zoal_atc::xplane
