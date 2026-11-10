#include "imgui_window.hpp"

#include <XPLMPanelGraphics.h>
#include <XPLMProcessing.h>

#include <cfloat>
#include <cstdint>
#include <utility>
#include <vector>

#include "imgui.h"

namespace zoal_atc::xplane {

// The panel-graphics helpers take ImGui's buffers as they are; these are the
// layouts they are documented to match (XPLMPanelGraphics.h, IMGUI HELPERS).
static_assert(sizeof(ImDrawVert) == 5 * sizeof(float),
              "XPLMMesh_t wants 20-byte vertices: pos, uv, packed RGBA8");
static_assert(sizeof(ImDrawIdx) == sizeof(std::uint16_t),
              "XPLMMesh_t wants 16-bit indices; keep ImDrawIdx at its default");

namespace {

void *texture_handle(ImTextureID id) {
  return reinterpret_cast<void *>(static_cast<std::uintptr_t>(id));
}

ImTextureID texture_id(void *handle) {
  return static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(handle));
}

// Restores whatever ImGui context was current, so one window's callbacks never
// leave another's context in place.
class ContextScope {
public:
  explicit ContextScope(ImGuiContext *context)
      : previous_(ImGui::GetCurrentContext()) {
    ImGui::SetCurrentContext(context);
  }
  ~ContextScope() { ImGui::SetCurrentContext(previous_); }
  ContextScope(const ContextScope &) = delete;
  ContextScope &operator=(const ContextScope &) = delete;

private:
  ImGuiContext *previous_;
};

} // namespace

ImguiWindow::ImguiWindow(int left, int top, int right, int bottom,
                         BuildFn build)
    : build_(std::move(build)) {
  context_ = ImGui::CreateContext();
  {
    ContextScope scope(context_);
    ImGuiIO &io = ImGui::GetIO();
    // Nothing is saved between sessions, and nothing written beside X-Plane.
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.BackendPlatformName = "zoal-atc xplm";
    io.BackendRendererName = "zoal-atc xplm panel graphics";
    // Textures are created, replaced and destroyed on ImGui's request
    // (update_texture), so fonts can be rasterized at any size and density.
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    io.Fonts->TexDesiredFormat = ImTextureFormat_RGBA32;
  }

  XPLMCreateWindow_t params = {};
  params.structSize = sizeof(params);
  params.left = left;
  params.top = top;
  params.right = right;
  params.bottom = bottom;
  params.visible = 0;
  params.drawWindowFunc = draw_cb;
  params.handleMouseClickFunc = click_cb;
  params.handleRightClickFunc = right_click_cb;
  params.handleKeyFunc = key_cb;
  params.handleCursorFunc = cursor_cb;
  params.handleMouseWheelFunc = wheel_cb;
  params.refcon = this;
  // No chrome of X-Plane's, and clicks inside the window stop here.
  params.decorateAsFloatingWindow = xplm_WindowDecorationSelfDecorated;
  params.layer = xplm_WindowLayerFloatingWindows;
  params.contentType = xplm_WindowContentTypePanelGraphics;
  window_ = XPLMCreateWindowEx(&params);
}

ImguiWindow::~ImguiWindow() {
  if (window_) {
    XPLMDestroyWindow(window_);
    window_ = nullptr;
  }
  if (context_) {
    destroy_textures();
    ImGui::DestroyContext(context_);
    context_ = nullptr;
  }
}

bool ImguiWindow::visible() const {
  return window_ && XPLMGetWindowIsVisible(window_) != 0;
}

void ImguiWindow::set_visible(bool visible) {
  if (window_) {
    XPLMSetWindowIsVisible(window_, visible ? 1 : 0);
  }
}

void ImguiWindow::set_geometry(int left, int top, int right, int bottom) {
  if (!window_) {
    return;
  }
  int l = 0, t = 0, r = 0, b = 0;
  XPLMGetWindowGeometry(window_, &l, &t, &r, &b);
  if (l != left || t != top || r != right || b != bottom) {
    XPLMSetWindowGeometry(window_, left, top, right, bottom);
  }
}

void ImguiWindow::geometry(int *left, int *top, int *right,
                           int *bottom) const {
  int l = 0, t = 0, r = 0, b = 0;
  if (window_) {
    XPLMGetWindowGeometry(window_, &l, &t, &r, &b);
  }
  if (left) *left = l;
  if (top) *top = t;
  if (right) *right = r;
  if (bottom) *bottom = b;
}

void ImguiWindow::set_framebuffer_scale(float scale) {
  if (scale > 0.0f) {
    framebuffer_scale_ = scale;
  }
}

// --- X-Plane callbacks ------------------------------------------------------

void ImguiWindow::draw_cb(XPLMWindowID, void *refcon) {
  static_cast<ImguiWindow *>(refcon)->draw();
}

int ImguiWindow::click_cb(XPLMWindowID, int x, int y, XPLMMouseStatus status,
                          void *refcon) {
  return static_cast<ImguiWindow *>(refcon)->click(x, y, status, 0);
}

int ImguiWindow::right_click_cb(XPLMWindowID, int x, int y,
                                XPLMMouseStatus status, void *refcon) {
  return static_cast<ImguiWindow *>(refcon)->click(x, y, status, 1);
}

void ImguiWindow::key_cb(XPLMWindowID, char, XPLMKeyFlags, char, void *, int) {}

XPLMCursorStatus ImguiWindow::cursor_cb(XPLMWindowID, int, int, void *) {
  return xplm_CursorArrow;
}

int ImguiWindow::wheel_cb(XPLMWindowID, int x, int y, int wheel, int clicks,
                          void *refcon) {
  auto *self = static_cast<ImguiWindow *>(refcon);
  ContextScope scope(self->context_);
  float lx = 0.0f, ly = 0.0f;
  self->mouse_to_local(x, y, &lx, &ly);
  ImGuiIO &io = ImGui::GetIO();
  io.AddMousePosEvent(lx, ly);
  const auto amount = static_cast<float>(clicks);
  io.AddMouseWheelEvent(wheel == 1 ? amount : 0.0f, wheel == 0 ? amount : 0.0f);
  return 1;
}

// --- input ------------------------------------------------------------------

// X-Plane reports the mouse in global boxels with y up; ImGui wants window-local
// boxels with y down from the top-left corner.
void ImguiWindow::mouse_to_local(int x, int y, float *out_x,
                                 float *out_y) const {
  int left = 0, top = 0, right = 0, bottom = 0;
  geometry(&left, &top, &right, &bottom);
  *out_x = static_cast<float>(x - left);
  *out_y = static_cast<float>(top - y);
}

int ImguiWindow::click(int x, int y, XPLMMouseStatus status, int button) {
  ContextScope scope(context_);
  float lx = 0.0f, ly = 0.0f;
  mouse_to_local(x, y, &lx, &ly);
  ImGuiIO &io = ImGui::GetIO();
  io.AddMousePosEvent(lx, ly);
  if (status == xplm_MouseDown) {
    io.AddMouseButtonEvent(button, true);
  } else if (status == xplm_MouseUp) {
    io.AddMouseButtonEvent(button, false);
  }
  // Every click inside the window is ours: X-Plane's docs warn that passing
  // clicks through a window breaks its mouse tracking. The window is kept no
  // bigger than what it shows, so nothing else is covered.
  return 1;
}

// --- frame ------------------------------------------------------------------

void ImguiWindow::draw() {
  ContextScope scope(context_);
  ImGuiIO &io = ImGui::GetIO();

  int left = 0, top = 0, right = 0, bottom = 0;
  geometry(&left, &top, &right, &bottom);
  io.DisplaySize = ImVec2(static_cast<float>(right - left),
                          static_cast<float>(top - bottom));
  io.DisplayFramebufferScale = ImVec2(framebuffer_scale_, framebuffer_scale_);

  const float now = XPLMGetElapsedTime();
  io.DeltaTime = (last_time_ > 0.0f && now > last_time_) ? now - last_time_
                                                          : 1.0f / 60.0f;
  last_time_ = now;

  // Hover follows the mouse wherever it is: X-Plane sends the cursor and click
  // callbacks only while the mouse is over the window, so the position is read
  // every frame, and parked off-window when it has left.
  int mx = 0, my = 0;
  XPLMGetMouseLocationGlobal(&mx, &my);
  if (mx >= left && mx < right && my > bottom && my <= top) {
    float lx = 0.0f, ly = 0.0f;
    mouse_to_local(mx, my, &lx, &ly);
    io.AddMousePosEvent(lx, ly);
  } else if (!ImGui::IsAnyMouseDown()) {
    io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
  }

  ImGui::NewFrame();
  if (build_) {
    build_();
  }
  ImGui::Render();

  ImDrawData *data = ImGui::GetDrawData();
  if (data->Textures != nullptr) {
    for (ImTextureData *texture : *data->Textures) {
      if (texture->Status != ImTextureStatus_OK) {
        update_texture(texture);
      }
    }
  }
  if (data->CmdListsCount == 0) {
    return;
  }

  // Panel graphics draws in global boxels, y up; ImGui's buffers are
  // window-local, y down. The transform does the whole conversion -- vertices
  // and clip rects are handed over untouched (XPLMPanelGraphics.h).
  XPLMTransformPush();
  XPLMTransformTranslate(static_cast<float>(left), static_cast<float>(top));
  XPLMTransformScale(1.0f, -1.0f);

  std::vector<XPLMDrawCall_t> calls;
  for (const ImDrawList *list : data->CmdLists) {
    calls.clear();
    for (const ImDrawCmd &cmd : list->CmdBuffer) {
      if (cmd.UserCallback != nullptr || cmd.ElemCount == 0) {
        continue;
      }
      XPLMDrawCall_t call = {};
      call.tex_ref = texture_handle(cmd.GetTexID());
      call.scissors[0] = cmd.ClipRect.x - data->DisplayPos.x;
      call.scissors[1] = cmd.ClipRect.y - data->DisplayPos.y;
      call.scissors[2] = cmd.ClipRect.z - data->DisplayPos.x;
      call.scissors[3] = cmd.ClipRect.w - data->DisplayPos.y;
      call.idx_offset = static_cast<int>(cmd.IdxOffset);
      call.element_count = static_cast<int>(cmd.ElemCount);
      call.vtx_offset = static_cast<int>(cmd.VtxOffset);
      calls.push_back(call);
    }
    if (calls.empty()) {
      continue;
    }
    XPLMMesh_t mesh = {};
    mesh.vertex_count = list->VtxBuffer.Size;
    mesh.vertices = reinterpret_cast<const float *>(list->VtxBuffer.Data);
    mesh.index_count = list->IdxBuffer.Size;
    mesh.indices = list->IdxBuffer.Data;
    XPLMDrawCalls(&mesh, static_cast<int>(calls.size()), calls.data());
  }

  XPLMTransformPop();
}

// --- textures ---------------------------------------------------------------

// XPLMCreateTexture uploads a whole image and has no partial update, so an
// update replaces the texture. They are rare -- a font rasterizing new glyphs
// -- and X-Plane keeps a destroyed texture alive until the frame drawing it is
// rendered, so replacing one mid-frame is safe.
void ImguiWindow::update_texture(ImTextureData *texture) {
  if (texture->Status == ImTextureStatus_WantCreate ||
      texture->Status == ImTextureStatus_WantUpdates) {
    if (texture->TexID != ImTextureID_Invalid) {
      XPLMDestroyTexture(texture_handle(texture->TexID));
    }
    void *handle = XPLMCreateTexture(
        static_cast<const unsigned char *>(texture->GetPixels()),
        texture->Width, texture->Height);
    texture->SetTexID(texture_id(handle));
    texture->SetStatus(ImTextureStatus_OK);
  } else if (texture->Status == ImTextureStatus_WantDestroy &&
             texture->UnusedFrames > 0) {
    if (texture->TexID != ImTextureID_Invalid) {
      XPLMDestroyTexture(texture_handle(texture->TexID));
    }
    texture->SetTexID(ImTextureID_Invalid);
    texture->SetStatus(ImTextureStatus_Destroyed);
  }
}

void ImguiWindow::destroy_textures() {
  ContextScope scope(context_);
  for (ImTextureData *texture : ImGui::GetPlatformIO().Textures) {
    if (texture->RefCount == 1 && texture->TexID != ImTextureID_Invalid) {
      XPLMDestroyTexture(texture_handle(texture->TexID));
      texture->SetTexID(ImTextureID_Invalid);
      texture->SetStatus(ImTextureStatus_Destroyed);
    }
  }
}

} // namespace zoal_atc::xplane
