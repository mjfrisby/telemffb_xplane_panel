// TelemFFB Panel for X-Plane
// Copyright (c) 2026 Micah Frisby
// Licensed under the GNU General Public License v3 - see LICENSE.

#include "ImGuiWindow.h"

#include <windows.h>
#include <GL/gl.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>

#include "XPLMGraphics.h"
#include "XPLMProcessing.h"
#include "imgui.h"
#include "imgui_impl_opengl2.h"

namespace {
constexpr float kFontSize = 17.0f;

// A point in drawing coordinates (boxels) as a framebuffer pixel, through the
// transform X-Plane set up for this window.
void toPixels(const GLfloat mv[16], const GLfloat proj[16], const GLint vp[4],
              float x, float y, float& px, float& py) {
    float eye[4], clip[4];
    const float p[4] = {x, y, 0.0f, 1.0f};
    for (int i = 0; i < 4; ++i)
        eye[i] = mv[i] * p[0] + mv[4 + i] * p[1] + mv[8 + i] * p[2] + mv[12 + i] * p[3];
    for (int i = 0; i < 4; ++i)
        clip[i] = proj[i] * eye[0] + proj[4 + i] * eye[1] + proj[8 + i] * eye[2] + proj[12 + i] * eye[3];
    const float w = clip[3] != 0.0f ? clip[3] : 1.0f;
    px = vp[0] + (clip[0] / w + 1.0f) * 0.5f * vp[2];
    py = vp[1] + (clip[1] / w + 1.0f) * 0.5f * vp[3];
}

void applyStyle() {
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 0.0f;
    style.FrameRounding = 6.0f;
    style.GrabRounding = 6.0f;
    style.ScrollbarRounding = 6.0f;
    style.WindowBorderSize = 0.0f;
    // TelemFFB's colors: the MSFS panel's background and purple
    ImVec4* c = style.Colors;
    const ImVec4 purple(0.416f, 0.184f, 0.690f, 1.0f);        // #6a2fb0
    const ImVec4 purpleLight(0.545f, 0.310f, 0.816f, 1.0f);   // #8b4fd0
    c[ImGuiCol_WindowBg] = ImVec4(0.078f, 0.086f, 0.102f, 1.0f);   // #14161a
    c[ImGuiCol_FrameBg] = ImVec4(0.165f, 0.180f, 0.220f, 1.0f);    // #2a2e38
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.212f, 0.227f, 0.275f, 1.0f);
    c[ImGuiCol_FrameBgActive] = purple;
    c[ImGuiCol_Button] = ImVec4(0.165f, 0.180f, 0.220f, 1.0f);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.267f, 0.290f, 0.345f, 1.0f);
    c[ImGuiCol_ButtonActive] = purple;
    c[ImGuiCol_Header] = purple;
    c[ImGuiCol_HeaderHovered] = purpleLight;
    c[ImGuiCol_HeaderActive] = purple;
    c[ImGuiCol_SliderGrab] = purple;
    c[ImGuiCol_SliderGrabActive] = purpleLight;
    c[ImGuiCol_CheckMark] = purpleLight;
    c[ImGuiCol_Separator] = ImVec4(0.200f, 0.224f, 0.278f, 1.0f);  // #333947
    c[ImGuiCol_Tab] = ImVec4(0.165f, 0.180f, 0.220f, 1.0f);
    c[ImGuiCol_TabHovered] = purpleLight;
    c[ImGuiCol_TabSelected] = purple;
    c[ImGuiCol_ScrollbarGrab] = ImVec4(0.267f, 0.290f, 0.345f, 1.0f);
    c[ImGuiCol_ScrollbarGrabHovered] = purple;
    c[ImGuiCol_ScrollbarGrabActive] = purpleLight;
}
}  // namespace

ImGuiWindow::ImGuiWindow(const char* title, int width, int height) {
    ImGuiContext* previous = ImGui::GetCurrentContext();
    context_ = ImGui::CreateContext();
    ImGui::SetCurrentContext(context_);
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    char windows[MAX_PATH];
    const std::string fonts = GetWindowsDirectoryA(windows, MAX_PATH) ? std::string(windows) + "\\Fonts\\" : "";
    auto load = [&](const char* file) -> ImFont* {
        const std::string path = fonts + file;
        if (fonts.empty() || GetFileAttributesA(path.c_str()) == INVALID_FILE_ATTRIBUTES) return nullptr;
        return io.Fonts->AddFontFromFileTTF(path.c_str(), kFontSize);
    };
    // the first one added is the default
    if (!load("segoeui.ttf")) io.Fonts->AddFontDefault();
    mono_ = load("consola.ttf");
    monoBold_ = load("consolab.ttf");
    applyStyle();
    ImGui_ImplOpenGL2_Init();
    ImGui::SetCurrentContext(previous);

    int screenLeft, screenTop, screenRight, screenBottom;
    XPLMGetScreenBoundsGlobal(&screenLeft, &screenTop, &screenRight, &screenBottom);
    XPLMCreateWindow_t params = {};
    params.structSize = sizeof(params);
    params.left = screenLeft + 80;
    params.top = screenTop - 120;
    params.right = params.left + width;
    params.bottom = params.top - height;
    params.visible = 0;
    params.drawWindowFunc = drawCallback;
    params.handleMouseClickFunc = leftClickCallback;
    params.handleRightClickFunc = rightClickCallback;
    params.handleMouseWheelFunc = wheelCallback;
    params.handleCursorFunc = cursorCallback;
    params.handleKeyFunc = keyCallback;
    params.refcon = this;
    params.decorateAsFloatingWindow = xplm_WindowDecorationRoundRectangle;
    params.layer = xplm_WindowLayerFloatingWindows;
    id_ = XPLMCreateWindowEx(&params);
    XPLMSetWindowPositioningMode(id_, xplm_WindowPositionFree, -1);
    XPLMSetWindowResizingLimits(id_, 320, 240, 1400, 1800);
    XPLMSetWindowTitle(id_, title);
}

ImGuiWindow::~ImGuiWindow() {
    if (id_) XPLMDestroyWindow(id_);
    ImGuiContext* previous = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(context_);
    ImGui_ImplOpenGL2_Shutdown();
    ImGui::DestroyContext(context_);
    ImGui::SetCurrentContext(previous == context_ ? nullptr : previous);
}

bool ImGuiWindow::isVisible() const {
    return id_ && XPLMGetWindowIsVisible(id_);
}

void ImGuiWindow::setVisible(bool visible) {
    if (!id_) return;
    XPLMSetWindowIsVisible(id_, visible ? 1 : 0);
    if (visible) XPLMBringWindowToFront(id_);
}

void ImGuiWindow::setVR(bool inVR) {
    if (id_) XPLMSetWindowPositioningMode(id_, inVR ? xplm_WindowVR : xplm_WindowPositionFree, -1);
}

void ImGuiWindow::drawCallback(XPLMWindowID, void* refcon) {
    static_cast<ImGuiWindow*>(refcon)->draw();
}

int ImGuiWindow::leftClickCallback(XPLMWindowID, int x, int y, XPLMMouseStatus status, void* refcon) {
    return static_cast<ImGuiWindow*>(refcon)->click(0, x, y, status);
}

int ImGuiWindow::rightClickCallback(XPLMWindowID, int x, int y, XPLMMouseStatus status, void* refcon) {
    return static_cast<ImGuiWindow*>(refcon)->click(1, x, y, status);
}

XPLMCursorStatus ImGuiWindow::cursorCallback(XPLMWindowID, int x, int y, void* refcon) {
    auto* self = static_cast<ImGuiWindow*>(refcon);
    ImGui::SetCurrentContext(self->context_);
    ImGui::GetIO().AddMousePosEvent(static_cast<float>(x - self->left_), static_cast<float>(self->top_ - y));
    return xplm_CursorDefault;
}

int ImGuiWindow::wheelCallback(XPLMWindowID, int x, int y, int wheel, int clicks, void* refcon) {
    auto* self = static_cast<ImGuiWindow*>(refcon);
    ImGui::SetCurrentContext(self->context_);
    ImGuiIO& io = ImGui::GetIO();
    io.AddMousePosEvent(static_cast<float>(x - self->left_), static_cast<float>(self->top_ - y));
    io.AddMouseWheelEvent(wheel == 1 ? static_cast<float>(clicks) : 0.0f,
                          wheel == 0 ? static_cast<float>(clicks) : 0.0f);
    return 1;
}

// Nothing in the panel takes typing, so the keyboard stays with X-Plane.
void ImGuiWindow::keyCallback(XPLMWindowID, char, XPLMKeyFlags, char, void*, int) {}

int ImGuiWindow::click(int button, int x, int y, XPLMMouseStatus status) {
    ImGui::SetCurrentContext(context_);
    ImGuiIO& io = ImGui::GetIO();
    io.AddMousePosEvent(static_cast<float>(x - left_), static_cast<float>(top_ - y));
    if (status == xplm_MouseDown) {
        io.AddMouseButtonEvent(button, true);
        XPLMBringWindowToFront(id_);
    } else if (status == xplm_MouseUp) {
        io.AddMouseButtonEvent(button, false);
    }
    return 1;
}

void ImGuiWindow::draw() {
    int left, top, right, bottom;
    const bool inVR = XPLMWindowIsInVR(id_) != 0;
    if (inVR) {
        int width, height;
        XPLMGetWindowGeometryVR(id_, &width, &height);
        left = 0;
        bottom = 0;
        right = width;
        top = height;
    } else {
        XPLMGetWindowGeometry(id_, &left, &top, &right, &bottom);
    }
    left_ = left;
    top_ = top;

    ImGui::SetCurrentContext(context_);
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(right - left), static_cast<float>(top - bottom));
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
    const float now = XPLMGetElapsedTime();
    io.DeltaTime = (lastTime_ > 0.0f && now > lastTime_) ? now - lastTime_ : 1.0f / 60.0f;
    lastTime_ = now;

    // Cursor callbacks stop once the mouse leaves, so hovering is cleared
    // here - unless a button is held, when a drag may carry on outside.
    if (!inVR && !io.MouseDown[0] && !io.MouseDown[1]) {
        int mx, my;
        XPLMGetMouseLocationGlobal(&mx, &my);
        if (mx < left || mx >= right || my < bottom || my >= top) io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
    }

    ImGui_ImplOpenGL2_NewFrame();
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("##content", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);
    buildInterface();
    ImGui::End();
    ImGui::Render();
    render(ImGui::GetDrawData());
}

void ImGuiWindow::render(ImDrawData* data) {
    if (data->Textures != nullptr)
        for (ImTextureData* tex : *data->Textures)
            if (tex->Status != ImTextureStatus_OK) ImGui_ImplOpenGL2_UpdateTexture(tex);

    // X-Plane's transform for this window, to turn clip rectangles into the
    // pixels glScissor wants - and any scissor it already has, to stay inside.
    GLfloat mv[16], proj[16];
    GLint vp[4], outer[4];
    glGetFloatv(GL_MODELVIEW_MATRIX, mv);
    glGetFloatv(GL_PROJECTION_MATRIX, proj);
    glGetIntegerv(GL_VIEWPORT, vp);
    const bool hadScissor = glIsEnabled(GL_SCISSOR_TEST) == GL_TRUE;
    glGetIntegerv(GL_SCISSOR_BOX, outer);

    XPLMSetGraphicsState(0, 1, 0, 0, 1, 0, 0);
    glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_TRANSFORM_BIT | GL_SCISSOR_BIT |
                 GL_POLYGON_BIT | GL_TEXTURE_BIT);
    glPushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_CULL_FACE);
    glEnable(GL_SCISSOR_TEST);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);

    // ImGui's y-down window space onto the window's boxels
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glTranslatef(static_cast<float>(left_), static_cast<float>(top_), 0.0f);
    glScalef(1.0f, -1.0f, 1.0f);

    for (const ImDrawList* list : data->CmdLists) {
        const ImDrawVert* vtx = list->VtxBuffer.Data;
        const ImDrawIdx* idx = list->IdxBuffer.Data;
        glVertexPointer(2, GL_FLOAT, sizeof(ImDrawVert), reinterpret_cast<const char*>(vtx) + offsetof(ImDrawVert, pos));
        glTexCoordPointer(2, GL_FLOAT, sizeof(ImDrawVert), reinterpret_cast<const char*>(vtx) + offsetof(ImDrawVert, uv));
        glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(ImDrawVert), reinterpret_cast<const char*>(vtx) + offsetof(ImDrawVert, col));

        for (const ImDrawCmd& cmd : list->CmdBuffer) {
            if (cmd.UserCallback) {
                if (cmd.UserCallback != ImDrawCallback_ResetRenderState) cmd.UserCallback(list, &cmd);
                continue;
            }
            float ax, ay, bx, by;
            toPixels(mv, proj, vp, left_ + cmd.ClipRect.x, top_ - cmd.ClipRect.y, ax, ay);
            toPixels(mv, proj, vp, left_ + cmd.ClipRect.z, top_ - cmd.ClipRect.w, bx, by);
            int x0 = static_cast<int>(std::floor(std::min(ax, bx)));
            int y0 = static_cast<int>(std::floor(std::min(ay, by)));
            int x1 = static_cast<int>(std::ceil(std::max(ax, bx)));
            int y1 = static_cast<int>(std::ceil(std::max(ay, by)));
            if (hadScissor) {
                x0 = std::max(x0, outer[0]);
                y0 = std::max(y0, outer[1]);
                x1 = std::min(x1, outer[0] + outer[2]);
                y1 = std::min(y1, outer[1] + outer[3]);
            }
            if (x1 <= x0 || y1 <= y0) continue;
            glScissor(x0, y0, x1 - x0, y1 - y0);
            XPLMBindTexture2d(static_cast<int>(static_cast<intptr_t>(cmd.GetTexID())), 0);
            glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(cmd.ElemCount),
                           sizeof(ImDrawIdx) == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT, idx + cmd.IdxOffset);
        }
    }

    glPopMatrix();
    glPopClientAttrib();
    glPopAttrib();
}
