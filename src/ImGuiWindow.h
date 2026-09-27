// TelemFFB Panel for X-Plane
// Copyright (c) 2026 Micah Frisby
// Licensed under the GNU General Public License v3 - see LICENSE.
//
// An X-Plane modern window with a Dear ImGui interface in it. ImGui draws
// in the window's own coordinates through whatever transform X-Plane has set
// up for window drawing, so the same code serves a window on the desktop and
// one placed in VR. Subclasses fill buildInterface().

#pragma once

#include "XPLMDisplay.h"

struct ImDrawData;
struct ImFont;
struct ImGuiContext;

class ImGuiWindow {
public:
    ImGuiWindow(const char* title, int width, int height);
    virtual ~ImGuiWindow();

    ImGuiWindow(const ImGuiWindow&) = delete;
    ImGuiWindow& operator=(const ImGuiWindow&) = delete;

    bool isVisible() const;
    virtual void setVisible(bool visible);
    void toggle() { setVisible(!isVisible()); }

    // Follow X-Plane into VR, or back out of it.
    void setVR(bool inVR);

protected:
    // The window's content, built once per drawn frame inside one ImGui window.
    virtual void buildInterface() = 0;

    // Consolas for numbers that should not shift as they change, and its bold
    // for badges; null where Windows has no Consolas, when the default serves.
    ImFont* monoFont() const { return mono_; }
    ImFont* monoBoldFont() const { return monoBold_; }

private:
    static void drawCallback(XPLMWindowID id, void* refcon);
    static int leftClickCallback(XPLMWindowID id, int x, int y, XPLMMouseStatus status, void* refcon);
    static int rightClickCallback(XPLMWindowID id, int x, int y, XPLMMouseStatus status, void* refcon);
    static XPLMCursorStatus cursorCallback(XPLMWindowID id, int x, int y, void* refcon);
    static int wheelCallback(XPLMWindowID id, int x, int y, int wheel, int clicks, void* refcon);
    static void keyCallback(XPLMWindowID id, char key, XPLMKeyFlags flags, char vkey, void* refcon, int losingFocus);

    void draw();
    int click(int button, int x, int y, XPLMMouseStatus status);
    void render(ImDrawData* data);

    XPLMWindowID id_ = nullptr;
    ImGuiContext* context_ = nullptr;
    ImFont* mono_ = nullptr;
    ImFont* monoBold_ = nullptr;
    // The window's top-left in drawing coordinates, as of the last draw:
    // mouse positions arrive in the same coordinates and are made local by it.
    int left_ = 0;
    int top_ = 0;
    float lastTime_ = 0.0f;
};
