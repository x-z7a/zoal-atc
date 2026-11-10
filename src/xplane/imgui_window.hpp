#ifndef ZOAL_ATC_XPLANE_IMGUI_WINDOW_HPP
#define ZOAL_ATC_XPLANE_IMGUI_WINDOW_HPP

#include <XPLMDisplay.h>

#include <functional>
#include <utility>

struct ImGuiContext;
struct ImTextureData;

namespace zoal_atc::xplane {

// ImguiWindow is an X-Plane window whose contents are a Dear ImGui frame.
//
// It is an ordinary plugin window, not a browser one: X-Plane renders it
// natively through the panel-graphics ImGui helpers (SDK 4.4, X-Plane 12.4.4),
// so a click on it never makes it X-Plane's "active browser" -- the state that
// blanks other plugins' UIs (Airfoillabs' XJet sidebar) while a browser window
// holds it.
//
// Each window owns its own ImGui context. It is the platform (geometry, time,
// mouse, keyboard) and the renderer (textures, draw calls) for that context;
// `build` is called once a frame between NewFrame and Render to lay out the
// contents.
class ImguiWindow {
public:
  using BuildFn = std::function<void()>;

  struct Options {
    // Takes X-Plane's keyboard focus while ImGui is editing text, and gives it
    // back the moment it stops: a sim's keys are the pilot's commands, so the
    // window holds them only while a field is being typed into.
    bool keyboard = false;
    // X-Plane resizes the window from its edges (the window still draws its
    // own chrome).
    bool resizable = false;
    int min_width = 0;
    int min_height = 0;
  };

  // Created hidden, at the given global boxel rectangle (y up).
  ImguiWindow(int left, int top, int right, int bottom, BuildFn build,
              Options options);
  ImguiWindow(int left, int top, int right, int bottom, BuildFn build)
      : ImguiWindow(left, top, right, bottom, std::move(build), Options{}) {}
  ~ImguiWindow();

  ImguiWindow(const ImguiWindow &) = delete;
  ImguiWindow &operator=(const ImguiWindow &) = delete;

  bool valid() const { return window_ != nullptr; }
  bool visible() const;
  void set_visible(bool visible);
  void bring_to_front();
  void set_geometry(int left, int top, int right, int bottom);
  void geometry(int *left, int *top, int *right, int *bottom) const;

  // The ImGui context, for setup that must happen with it current (fonts,
  // style). Make it current with ImGui::SetCurrentContext first.
  ImGuiContext *context() const { return context_; }

  // Physical pixels per boxel. ImGui lays out in boxels and rasterizes fonts at
  // this density, so text stays sharp on a scaled UI or a Retina screen.
  void set_framebuffer_scale(float scale);
  float framebuffer_scale() const { return framebuffer_scale_; }

private:
  static void draw_cb(XPLMWindowID, void *refcon);
  static int click_cb(XPLMWindowID, int x, int y, XPLMMouseStatus status,
                      void *refcon);
  static int right_click_cb(XPLMWindowID, int x, int y, XPLMMouseStatus status,
                            void *refcon);
  static void key_cb(XPLMWindowID, char, XPLMKeyFlags, char, void *, int);
  static XPLMCursorStatus cursor_cb(XPLMWindowID, int x, int y, void *refcon);
  static int wheel_cb(XPLMWindowID, int x, int y, int wheel, int clicks,
                      void *refcon);

  void draw();
  int click(int x, int y, XPLMMouseStatus status, int button);
  void key(char key, XPLMKeyFlags flags, char virtual_key, bool losing_focus);
  void sync_keyboard_focus();
  void mouse_to_local(int x, int y, float *out_x, float *out_y) const;
  void update_texture(ImTextureData *texture);
  void destroy_textures();

  XPLMWindowID window_ = nullptr;
  ImGuiContext *context_ = nullptr;
  BuildFn build_;
  Options options_;
  // The cursor ImGui asked for last frame, for X-Plane's cursor callback.
  int cursor_ = 0;
  // Set while this window hands the keyboard back itself, so the lost-focus
  // callback that follows is not mistaken for the pilot clicking away.
  bool releasing_focus_ = false;
  float last_time_ = 0.0f;
  float framebuffer_scale_ = 1.0f;
};

} // namespace zoal_atc::xplane

#endif
