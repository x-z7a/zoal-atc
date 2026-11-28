// panel_preview renders the radio panel off-screen, to a PPM image, with no
// X-Plane: the same PanelSession and the same draw_panel the plugin runs, fed a
// scripted console, rasterized in software.
//
//   panel_preview <fonts-dir> <out.ppm> [home|settings] [graphite|daylight|night]
//                 [width height] [scale] [full|empty|unreachable|armed]
//                 [mouse_x mouse_y]
//   panel_preview <fonts-dir> <out.ppm> hud [graphite|daylight|night]
//                 [width height] [scale] <scene.json> [open-choice-key] [typed]
//                 [no-transcript]
//
// The HUD mode draws the radio HUD from a scene the console's own controller
// tests write (atc internal/scenario, ZOAL_ATC_HUD_SCENES): the callsign, the
// channel, the last lines said and the menu that position offered, played into
// a HudSession as the console's events and drawn by the plugin's HUD code.
//
// It exists so a change to the panel's look can be checked -- and compared
// across the three themes -- without starting the sim.

#include "../src/ui/hud_view.hpp"
#include "../src/ui/kit.hpp"
#include "../src/ui/panel_view.hpp"
#include "../src/ui/traffic_labels_view.hpp"
#include "zoal_atc/gui/gui_bridge.hpp"
#include "zoal_atc/gui/hud_session.hpp"
#include "zoal_atc/gui/panel_session.hpp"
#include "zoal_atc/transport/json_util.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "imgui.h"

namespace {

using namespace zoal_atc;

// --- a software renderer for ImGui's draw data ---------------------------------

struct Image {
  int width = 0;
  int height = 0;
  std::vector<float> rgba; // premultiplied? no: straight, composited as we go

  void resize(int w, int h) {
    width = w;
    height = h;
    rgba.assign(static_cast<std::size_t>(w * h * 4), 0.0f);
  }
};

std::map<ImTextureID, ImTextureData *> g_textures;
ImTextureID g_next_texture = 1;

void update_textures(ImDrawData *data) {
  if (data->Textures == nullptr) {
    return;
  }
  for (ImTextureData *texture : *data->Textures) {
    if (texture->Status == ImTextureStatus_WantCreate ||
        texture->Status == ImTextureStatus_WantUpdates) {
      if (texture->TexID == ImTextureID_Invalid) {
        texture->SetTexID(g_next_texture++);
      }
      g_textures[texture->TexID] = texture;
      texture->SetStatus(ImTextureStatus_OK);
    } else if (texture->Status == ImTextureStatus_WantDestroy &&
               texture->UnusedFrames > 0) {
      g_textures.erase(texture->TexID);
      texture->SetTexID(ImTextureID_Invalid);
      texture->SetStatus(ImTextureStatus_Destroyed);
    }
  }
}

void sample(const ImTextureData *t, float u, float v, float out[4]) {
  if (t == nullptr) {
    out[0] = out[1] = out[2] = out[3] = 1.0f;
    return;
  }
  const float x = u * static_cast<float>(t->Width) - 0.5f;
  const float y = v * static_cast<float>(t->Height) - 0.5f;
  const int x0 = static_cast<int>(std::floor(x));
  const int y0 = static_cast<int>(std::floor(y));
  const float fx = x - static_cast<float>(x0);
  const float fy = y - static_cast<float>(y0);
  const auto *pixels = static_cast<const unsigned char *>(t->Pixels);
  auto at = [&](int px, int py, int c) {
    px = std::clamp(px, 0, t->Width - 1);
    py = std::clamp(py, 0, t->Height - 1);
    return static_cast<float>(pixels[(py * t->Width + px) * 4 + c]) / 255.0f;
  };
  for (int c = 0; c < 4; ++c) {
    const float top = at(x0, y0, c) * (1 - fx) + at(x0 + 1, y0, c) * fx;
    const float bottom = at(x0, y0 + 1, c) * (1 - fx) + at(x0 + 1, y0 + 1, c) * fx;
    out[c] = top * (1 - fy) + bottom * fy;
  }
}

void render(const ImDrawData *data, Image &image, float scale) {
  for (const ImDrawList *list : data->CmdLists) {
    const ImDrawVert *vertices = list->VtxBuffer.Data;
    const ImDrawIdx *indices = list->IdxBuffer.Data;
    for (const ImDrawCmd &cmd : list->CmdBuffer) {
      if (cmd.UserCallback != nullptr) {
        continue;
      }
      const auto found = g_textures.find(cmd.GetTexID());
      const ImTextureData *texture =
          found == g_textures.end() ? nullptr : found->second;
      const int clip_x0 = std::max(0, static_cast<int>(cmd.ClipRect.x * scale));
      const int clip_y0 = std::max(0, static_cast<int>(cmd.ClipRect.y * scale));
      const int clip_x1 =
          std::min(image.width, static_cast<int>(std::ceil(cmd.ClipRect.z * scale)));
      const int clip_y1 =
          std::min(image.height, static_cast<int>(std::ceil(cmd.ClipRect.w * scale)));
      for (unsigned i = 0; i + 2 < cmd.ElemCount; i += 3) {
        const ImDrawVert &a = vertices[cmd.VtxOffset + indices[cmd.IdxOffset + i]];
        const ImDrawVert &b = vertices[cmd.VtxOffset + indices[cmd.IdxOffset + i + 1]];
        const ImDrawVert &c = vertices[cmd.VtxOffset + indices[cmd.IdxOffset + i + 2]];
        const float ax = a.pos.x * scale, ay = a.pos.y * scale;
        const float bx = b.pos.x * scale, by = b.pos.y * scale;
        const float cx = c.pos.x * scale, cy = c.pos.y * scale;
        const float area = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
        if (std::fabs(area) < 1e-6f) {
          continue;
        }
        const int x0 = std::max(clip_x0, static_cast<int>(std::floor(std::min({ax, bx, cx}))));
        const int y0 = std::max(clip_y0, static_cast<int>(std::floor(std::min({ay, by, cy}))));
        const int x1 = std::min(clip_x1, static_cast<int>(std::ceil(std::max({ax, bx, cx}))));
        const int y1 = std::min(clip_y1, static_cast<int>(std::ceil(std::max({ay, by, cy}))));
        auto colour = [](ImU32 col, int shift) {
          return static_cast<float>((col >> shift) & 0xFF) / 255.0f;
        };
        for (int py = y0; py < y1; ++py) {
          for (int px = x0; px < x1; ++px) {
            const float sx = static_cast<float>(px) + 0.5f;
            const float sy = static_cast<float>(py) + 0.5f;
            const float w0 = ((bx - sx) * (cy - sy) - (by - sy) * (cx - sx)) / area;
            const float w1 = ((cx - sx) * (ay - sy) - (cy - sy) * (ax - sx)) / area;
            const float w2 = 1.0f - w0 - w1;
            if (w0 < 0 || w1 < 0 || w2 < 0) {
              continue;
            }
            const float u = w0 * a.uv.x + w1 * b.uv.x + w2 * c.uv.x;
            const float v = w0 * a.uv.y + w1 * b.uv.y + w2 * c.uv.y;
            float tex[4];
            sample(texture, u, v, tex);
            float src[4];
            const int shifts[4] = {IM_COL32_R_SHIFT, IM_COL32_G_SHIFT,
                                   IM_COL32_B_SHIFT, IM_COL32_A_SHIFT};
            for (int k = 0; k < 4; ++k) {
              src[k] = (w0 * colour(a.col, shifts[k]) + w1 * colour(b.col, shifts[k]) +
                        w2 * colour(c.col, shifts[k])) *
                       tex[k];
            }
            float *dst = &image.rgba[static_cast<std::size_t>((py * image.width + px) * 4)];
            const float alpha = src[3];
            for (int k = 0; k < 3; ++k) {
              dst[k] = src[k] * alpha + dst[k] * (1.0f - alpha);
            }
            dst[3] = alpha + dst[3] * (1.0f - alpha);
          }
        }
      }
    }
  }
}

// Behind the glass, a sky: so the panel's translucency shows as it would in
// the sim.
void paint_sky(Image &image) {
  for (int y = 0; y < image.height; ++y) {
    const float t = static_cast<float>(y) / static_cast<float>(image.height);
    for (int x = 0; x < image.width; ++x) {
      float *px = &image.rgba[static_cast<std::size_t>((y * image.width + x) * 4)];
      const float band = (x / 160 + y / 160) % 2 == 0 ? 0.03f : 0.0f;
      px[0] = 0.36f + 0.30f * t + band;
      px[1] = 0.52f + 0.22f * t + band;
      px[2] = 0.78f + 0.08f * t + band;
      px[3] = 1.0f;
    }
  }
}

bool write_ppm(const Image &image, const std::string &path) {
  std::ofstream out(path, std::ios::binary);
  out << "P6\n" << image.width << " " << image.height << "\n255\n";
  for (std::size_t i = 0; i < image.rgba.size(); i += 4) {
    for (int k = 0; k < 3; ++k) {
      const float v = std::clamp(image.rgba[i + static_cast<std::size_t>(k)], 0.0f, 1.0f);
      out.put(static_cast<char>(std::lround(std::pow(v, 1.0f) * 255.0f)));
    }
  }
  return static_cast<bool>(out);
}

// --- a scripted console -----------------------------------------------------------

std::string gui_event(const std::string &name, const std::string &payload) {
  return R"({"type":"gui_event","event":")" + name + R"(","payload":)" + payload + "}";
}

std::string gui_ok(std::uint64_t id, const std::string &payload = "{}") {
  return R"({"type":"gui_response","request_id":)" + std::to_string(id) +
         R"(,"ok":true,"payload":)" + payload + "}";
}

std::string entry(const std::string &kind, const std::string &text,
                  const std::string &spoken_by, const std::string &at,
                  bool overheard = false, const std::string &to = "") {
  std::string out = R"({"kind":")" + kind + R"(","text":")" + text +
                    R"(","spokenBy":")" + spoken_by + R"(","at":")" + at + "\"";
  if (overheard) {
    out += R"(,"overheard":true,"addressedTo":")" + to + "\"";
  }
  return out + "}";
}

// --- the HUD, from a scene ----------------------------------------------------

std::string read_all(const std::string &path) {
  std::ifstream in(path, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

std::string quoted(const std::string &value) {
  return "\"" + transport::json_escape(value) + "\"";
}

// The scene's menu (contract.Menu) as the console's pilot_menu event carries it.
std::string menu_event(const std::string &scene) {
  const std::string menu = transport::json_raw_field(scene, "menu").value_or("{}");
  std::string items = "[";
  if (const auto raw = transport::json_raw_field(menu, "items")) {
    if (const auto elements = transport::json_array_elements(*raw)) {
      bool first = true;
      for (const std::string &item : *elements) {
        items += first ? "" : ",";
        first = false;
        items += item;
      }
    }
  }
  items += "]";
  const std::uint64_t next =
      transport::json_uint_field(menu, "next_frequency_hz").value_or(0);
  return R"({"frequencyHz":)" +
         std::to_string(transport::json_uint_field(scene, "frequencyHz").value_or(0)) +
         R"(,"role":)" + quoted(transport::json_string_field(scene, "role").value_or("")) +
         R"(,"station":)" + quoted(transport::json_string_field(scene, "station").value_or("")) +
         R"(,"revision":1,"nextFrequencyHz":)" + std::to_string(next) +
         R"(,"items":)" + items + "}";
}

int render_hud(const std::string &fonts, const std::string &out, const std::string &theme,
               int width, int height, float scale, const std::string &scene_path,
               const std::string &open_choice, const std::string &typed,
               bool transcript, bool collapsed) {
  const std::string scene = read_all(scene_path);
  if (scene.empty()) {
    std::cerr << "no scene at " << scene_path << "\n";
    return 2;
  }
  gui::GuiBridge bridge;
  bridge.set_local_action([&](const std::string &action, const std::string &)
                              -> std::optional<gui::LocalAnswer> {
    if (action == "settings") {
      return gui::LocalAnswer{true, R"({"theme":")" + theme + R"("})", ""};
    }
    return std::nullopt;
  });
  gui::HudSession session(bridge);

  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  io.IniFilename = nullptr;
  io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
  io.Fonts->TexDesiredFormat = ImTextureFormat_RGBA32;
  io.DisplaySize = ImVec2(static_cast<float>(width), static_cast<float>(height));
  io.DisplayFramebufferScale = ImVec2(scale, scale);
  const ui::Typeface face = ui::load_typeface(
      fonts, fonts, [](const std::string &line) { std::cerr << line << "\n"; });
  ImGui::GetStyle().ItemSpacing = ImVec2(0.0f, 0.0f);

  const std::int64_t wall = 1'794'052'800'000;
  const std::string at = "2026-11-07T12:00:00Z";
  bridge.set_connected(true, 0);
  session.attach(0);
  for (const std::string &frame : bridge.take_socket_frames()) {
    if (transport::json_string_field(frame, "type").value_or("") == "gui_subscribe") {
      bridge.on_socket_frame(
          gui_ok(transport::json_uint_field(frame, "request_id").value_or(0)), 0);
    }
  }
  session.pump(0, wall);
  const std::uint64_t channel = transport::json_uint_field(scene, "frequencyHz").value_or(0);
  session.set_radio(1, channel, 0);
  bridge.on_socket_frame(
      gui_event("flight_snapshot",
                R"({"hasSession":true,"callsign":)" +
                    quoted(transport::json_string_field(scene, "callsign").value_or("")) + "}"),
      0);
  if (const auto raw = transport::json_raw_field(scene, "lines")) {
    if (const auto lines = transport::json_array_elements(*raw)) {
      for (const std::string &line : *lines) {
        bridge.on_socket_frame(
            gui_event("comm_log",
                      entry(transport::json_string_field(line, "kind").value_or("atc"),
                            transport::json_escape(
                                transport::json_string_field(line, "text").value_or("")),
                            transport::json_string_field(line, "spokenBy").value_or(""), at)),
            0);
      }
    }
  }
  bridge.on_socket_frame(gui_event("pilot_menu", menu_event(scene)), 0);

  ui::HudViewState state;
  std::snprintf(state.typed, sizeof(state.typed), "%s", typed.c_str());
  if (!transcript) {
    session.pump(0, wall);
    session.on_settings_changed(R"({"hudTranscript":false})");
  }
  if (!open_choice.empty()) {
    session.pump(0, wall);
    session.toggle_choice(open_choice);
  }
  for (int i = 0; i < 4; ++i) {
    session.pump(0, wall);
    bridge.take_socket_frames();
    io.DeltaTime = 1.0f / 60.0f;
    io.AddMousePosEvent(-1.0f, -1.0f);
    ImGui::NewFrame();
    ui::draw_hud(session, state, face, ui::HudFrame{wall, collapsed});
    ImGui::Render();
    update_textures(ImGui::GetDrawData());
  }
  Image image;
  image.resize(static_cast<int>(static_cast<float>(width) * scale),
               static_cast<int>(static_cast<float>(height) * scale));
  paint_sky(image);
  render(ImGui::GetDrawData(), image, scale);
  ImGui::DestroyContext();
  if (!write_ppm(image, out)) {
    std::cerr << "could not write " << out << "\n";
    return 1;
  }
  return 0;
}

// The traffic tags over the sky: one at each distance band, a pilot on the
// ground, and a tracked and a followed one.
int render_labels(const std::string &fonts, const std::string &out, const std::string &theme,
                  int width, int height, float scale) {
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  io.IniFilename = nullptr;
  io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
  io.Fonts->TexDesiredFormat = ImTextureFormat_RGBA32;
  io.DisplaySize = ImVec2(static_cast<float>(width), static_cast<float>(height));
  io.DisplayFramebufferScale = ImVec2(scale, scale);
  const ui::Typeface face = ui::load_typeface(
      fonts, fonts, [](const std::string &line) { std::cerr << line << "\n"; });

  auto tag = [](const char *id, traffic::TrafficKind kind, const char *callsign,
                const char *type, const char *airline, double nm, double alt, double gs,
                double vs, bool ground, double camera_m, const char *tracked,
                const char *followed) {
    traffic::Nearby n;
    n.identity.id = id;
    n.identity.kind = kind;
    n.identity.callsign = callsign;
    n.identity.icao_type = type;
    n.identity.airline = airline;
    n.has_own = true;
    n.distance_nm = nm;
    n.sample.alt_ft_msl = alt;
    n.sample.groundspeed_kts = gs;
    n.sample.vertical_speed_fpm = vs;
    n.sample.on_ground = ground;
    return gui::traffic_label(n, camera_m, gui::LabelRange{}, tracked, followed);
  };
  using traffic::TrafficKind;
  const float w = static_cast<float>(width);
  const float h = static_cast<float>(height);
  std::vector<ui::PlacedLabel> labels = {
      {tag("ai:ACA451", TrafficKind::Ai, "ACA451", "A320", "ACA", 1.2, 3400, 180, -750, false, 1800, "", ""),
       ImVec2(w * 0.22f, h * 0.55f), 0.90},
      {tag("participant:a", TrafficKind::Participant, "BLS382", "BE99", "BLS", 0.4, 653, 12, 0, true, 700, "", ""),
       ImVec2(w * 0.50f, h * 0.80f), 0.80},
      {tag("ai:JZA7721", TrafficKind::Ai, "JZA7721", "DH8D", "JZA", 4.1, 6500, 230, 1500, false, 7500, "ai:JZA7721", "ai:JZA7721"),
       ImVec2(w * 0.74f, h * 0.42f), 0.95},
      {tag("ai:WJA612", TrafficKind::Ai, "WJA612", "B38M", "WJA", 6.2, 11000, 290, 0, false, 9000, "", ""),
       ImVec2(w * 0.40f, h * 0.30f), 0.97},
      {tag("ai:POE233", TrafficKind::Ai, "POE233", "DH8D", "POE", 13.5, 15000, 300, 0, false, 25000, "", ""),
       ImVec2(w * 0.62f, h * 0.20f), 0.99},
      {tag("ai:ACA88", TrafficKind::Ai, "ACA88", "B77W", "ACA", 15.0, 37000, 480, 0, false, 28500, "", ""),
       ImVec2(w * 0.12f, h * 0.18f), 0.995},
  };
  for (int i = 0; i < 4; ++i) {
    io.DeltaTime = 1.0f / 60.0f;
    ImGui::NewFrame();
    ui::draw_traffic_labels(labels, gui::hud_theme_from_name(theme), face);
    ImGui::Render();
    update_textures(ImGui::GetDrawData());
  }
  Image image;
  image.resize(static_cast<int>(w * scale), static_cast<int>(h * scale));
  paint_sky(image);
  render(ImGui::GetDrawData(), image, scale);
  ImGui::DestroyContext();
  if (!write_ppm(image, out)) {
    std::cerr << "could not write " << out << "\n";
    return 1;
  }
  return 0;
}

} // namespace

int main(int argc, char **argv) {
  if (argc < 3) {
    std::cerr << "usage: panel_preview <fonts-dir> <out.ppm> [home|settings] "
                 "[graphite|daylight|night] [width height] [scale] "
                 "[full|empty|unreachable|armed] "
                 "[mouse_x mouse_y]\n";
    return 2;
  }
  const std::string fonts = argv[1];
  const std::string out = argv[2];
  const std::string tab = argc > 3 ? argv[3] : "home";
  const std::string theme = argc > 4 ? argv[4] : "graphite";
  const int width = argc > 6 ? std::atoi(argv[5]) : 1200;
  const int height = argc > 6 ? std::atoi(argv[6]) : 800;
  const float scale = argc > 7 ? static_cast<float>(std::atof(argv[7])) : 1.0f;
  if (tab == "labels") {
    return render_labels(fonts, out, theme, width, height, scale);
  }
  if (tab == "hud") {
    if (argc < 9) {
      std::cerr << "usage: panel_preview <fonts-dir> <out.ppm> hud <theme> <width> <height> <scale> <scene.json>\n";
      return 2;
    }
    return render_hud(fonts, out, theme, width, height, scale, argv[8],
                      argc > 9 ? argv[9] : "", argc > 10 ? argv[10] : "",
                      !(argc > 11 && std::string(argv[11]) == "no-transcript"),
                      argc > 11 && std::string(argv[11]) == "collapsed");
  }
  const std::string mode = argc > 8 ? argv[8] : "full";
  const float mouse_x = argc > 10 ? static_cast<float>(std::atof(argv[9])) : -1.0f;
  const float mouse_y = argc > 10 ? static_cast<float>(std::atof(argv[10])) : -1.0f;

  // The plugin's local answers, as xplane_plugin.cpp gives them.
  gui::GuiBridge bridge;
  bridge.set_local_action([&](const std::string &action, const std::string &)
                              -> std::optional<gui::LocalAnswer> {
    if (action == "settings") {
      return gui::LocalAnswer{
          true, R"({"simbriefPilotId":"412933","theme":")" + theme + R"("})", ""};
    }
    if (action == "connection_settings") {
      return gui::LocalAnswer{true,
                              R"({"token":"s3cret-token","url":"wss://atc.zoal.app/plugin"})",
                              ""};
    }
    return std::nullopt;
  });
  gui::PanelSession session(bridge);

  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  io.IniFilename = nullptr;
  io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
  io.Fonts->TexDesiredFormat = ImTextureFormat_RGBA32;
  io.DisplaySize = ImVec2(static_cast<float>(width), static_cast<float>(height));
  io.DisplayFramebufferScale = ImVec2(scale, scale);
  const ui::Typeface face = ui::load_typeface(
      fonts, fonts, [](const std::string &line) { std::cerr << line << "\n"; });
  ui::setup_panel_style();

  ui::PanelViewState state;
  state.tab = tab == "settings"  ? ui::kSettingsTab
              : tab == "traffic" ? ui::kTrafficTab
                                 : ui::kHomeTab;

  const std::int64_t wall = 1'794'052'800'000;
  bridge.set_connected(true, 0);
  session.attach(0);
  for (const std::string &frame : bridge.take_socket_frames()) {
    const auto type = transport::json_string_field(frame, "type").value_or("");
    if (type == "gui_subscribe") {
      bridge.on_socket_frame(
          gui_ok(transport::json_uint_field(frame, "request_id").value_or(0)), 0);
    }
  }
  session.pump(0, wall);
  // The resync and plan read the link coming up asked for.
  for (const std::string &frame : bridge.take_socket_frames()) {
    const auto action = transport::json_string_field(frame, "action").value_or("");
    const auto id = transport::json_uint_field(frame, "request_id").value_or(0);
    if (action == "comm_log") {
      std::string log = "[]";
      if (mode != "empty") {
        log = "[" +
              entry("pilot", "Thunder Bay ground, Air Canada two one, at gate four with information Bravo, request taxi", "", "2026-11-07T11:58:00Z") + "," +
              entry("atc", "Air Canada two one, Thunder Bay ground, taxi to runway two five via Alpha, hold short of runway three one", "ground", "2026-11-07T11:58:10Z") + "," +
              entry("pilot", "Taxi two five via Alpha, hold short three one, Air Canada two one", "", "2026-11-07T11:58:20Z") + "," +
              entry("atc", "Bearskin three eight two, cleared to land runway two five", "tower", "2026-11-07T11:58:40Z", true, "BLS382") + "," +
              entry("atc", "Air Canada two one, contact tower one one eight decimal one", "ground", "2026-11-07T11:59:00Z") + "," +
              entry("atc", "Air Canada two one, Thunder Bay tower, runway two five, wind two six zero at eight, cleared for takeoff", "tower", "2026-11-07T11:59:30Z") + "]";
      }
      bridge.on_socket_frame(gui_ok(id, log), 0);
    } else if (action == "flight_plan") {
      bridge.on_socket_frame(
          gui_ok(id, R"({"hasPlan":true,"origin":"CYQT","destination":"CYYZ","cruiseAltFt":24000,"aircraftIcao":"DH8D","firstFix":"OPKAM","route":"OPKAM J501 VIBRO DUXUT"})"),
          0);
    }
  }
  bridge.on_socket_frame(
      gui_event("telemetry",
                R"({"frequencyMhz":118.1,"standbyMhz":121.9,"altitudeFtMsl":2150,"heightAglFt":0,"groundspeedKts":12,"onGround":false,"airport":"CYQT","surveillance":"current","trafficCount":3})"),
      0);
  bridge.on_socket_frame(
      gui_event("flight_snapshot",
                R"({"hasSession":true,"callsign":"ACA21","lifecycle":"departure"})"),
      0);
  bridge.on_socket_frame(gui_event("facility_snapshot", R"({"count":4})"), 0);

  if (mode == "unreachable") {
    // The console gone: the banner, dimmed readouts, an amber light.
    bridge.set_connected(false, 0);
  }
  if (mode == "armed") {
    session.press_new_flight(0, wall);
  }

  std::vector<std::function<void()>> later;
  ui::PanelFrame frame{"0.1.0", 0, wall, &later};

  // The Traffic tab: a few of the console's aircraft round CYQT, one tracked
  // and followed ("traffic" mode "empty" shows none).
  auto nearby = [](const char *id, traffic::TrafficKind kind, const char *callsign,
                   const char *type, const char *airline, double nm, double brg,
                   double alt, double rel, double gs, double vs, bool ground) {
    traffic::Nearby n;
    n.identity.id = id;
    n.identity.kind = kind;
    n.identity.callsign = callsign;
    n.identity.icao_type = type;
    n.identity.airline = airline;
    n.has_own = true;
    n.distance_nm = nm;
    n.bearing_deg = brg;
    n.relative_alt_ft = rel;
    n.sample.alt_ft_msl = alt;
    n.sample.groundspeed_kts = gs;
    n.sample.vertical_speed_fpm = vs;
    n.sample.on_ground = ground;
    return n;
  };
  std::vector<traffic::Nearby> picture;
  if (mode != "empty") {
    using traffic::TrafficKind;
    picture = {
        nearby("participant:b", TrafficKind::Participant, "CGXYZ", "C172", "", 0.4, 190, 653, 0, 8, 0, true),
        nearby("participant:a", TrafficKind::Participant, "BLS382", "BE99", "BLS", 2.1, 254, 2600, 450, 140, -700, false),
        nearby("ai:JZA7721", TrafficKind::Ai, "JZA7721", "DH8D", "JZA", 6.8, 71, 9000, 6850, 245, 1500, false),
        nearby("ai:ACA451", TrafficKind::Ai, "ACA451", "A320", "ACA", 14.3, 112, 24000, 21850, 410, 0, false),
        nearby("ai:WJA612", TrafficKind::Ai, "WJA612", "B38M", "WJA", 22.9, 300, 35000, 32850, 452, -1800, false),
    };
  }
  gui::TrafficPanel traffic = gui::traffic_panel(picture, state.traffic_filter,
                                                 "ai:JZA7721", "ai:JZA7721");
  traffic.chase_distance_m = 60.0;
  traffic.csl.folder = "/X-Plane 12/Resources/plugins/LiveTraffic/Resources/CSL";
  traffic.csl.models = 1874;
  if (mode == "nocsl") {
    // No models anywhere: the tab is the folder picker.
    traffic = gui::TrafficPanel{};
    traffic.unavailable = "No CSL models found, so traffic is off.";
    traffic.csl.missing = true;
    traffic.csl.found = {
        {"/X-Plane 12/Resources/plugins/Swift/data/csl", "Swift/data/csl"},
        {"/X-Plane 12/Resources/plugins/XSquawkBox/Resources/CSL", "XSquawkBox/Resources/CSL"},
    };
    traffic.csl.error =
        "That folder has no CSL models: there is no xsb_aircraft.txt in it or below it.";
    std::snprintf(state.csl_path, sizeof(state.csl_path), "%s", "~/Downloads/Bluebell");
  }
  const ui::TrafficActions actions{};
  frame.traffic = &traffic;
  frame.traffic_actions = &actions;
  Image image;
  for (int i = 0; i < 4; ++i) {
    session.pump(0, wall);
    io.DeltaTime = 1.0f / 60.0f;
    io.AddMousePosEvent(mouse_x, mouse_y);
    ImGui::NewFrame();
    ui::draw_panel(session, state, face, frame);
    ImGui::Render();
    update_textures(ImGui::GetDrawData());
    later.clear();
  }
  image.resize(static_cast<int>(static_cast<float>(width) * scale),
               static_cast<int>(static_cast<float>(height) * scale));
  paint_sky(image);
  render(ImGui::GetDrawData(), image, scale);
  ImGui::DestroyContext();
  if (!write_ppm(image, out)) {
    std::cerr << "could not write " << out << "\n";
    return 1;
  }
  return 0;
}
