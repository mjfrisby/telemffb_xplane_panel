// TelemFFB Panel for X-Plane
// Copyright (c) 2026 Micah Frisby
// Licensed under the GNU General Public License v3 - see LICENSE.
//
// Plugin entry points: a Plugins menu item and a bindable command to show
// the panel, and following X-Plane into and out of VR.

#include <cstring>
#include <memory>

#include "XPLMDataAccess.h"
#include "XPLMMenus.h"
#include "XPLMPlugin.h"
#include "XPLMUtilities.h"

#include "ApiClient.h"
#include "PanelWindow.h"
#include "version.h"

namespace {
std::unique_ptr<ApiClient> gApi;
std::unique_ptr<PanelWindow> gPanel;
XPLMCommandRef gToggle = nullptr;
XPLMMenuID gMenu = nullptr;

bool vrEnabled() {
    XPLMDataRef ref = XPLMFindDataRef("sim/graphics/VR/enabled");
    return ref && XPLMGetDatai(ref) != 0;
}

int onToggle(XPLMCommandRef, XPLMCommandPhase phase, void*) {
    if (phase != xplm_CommandBegin || !gApi) return 0;
    if (!gPanel) {
        gPanel = std::make_unique<PanelWindow>(*gApi);
        gPanel->setVR(vrEnabled());
    }
    gPanel->toggle();
    return 0;
}
}  // namespace

PLUGIN_API int XPluginStart(char* outName, char* outSig, char* outDesc) {
    std::strcpy(outName, "TelemFFB Panel");
    std::strcpy(outSig, "vpforce.telemffb.panel");
    std::strcpy(outDesc, "TelemFFB's settings and monitor in X-Plane (v" PANEL_VERSION ")");

    gToggle = XPLMCreateCommand("TelemFFB/panel/toggle", "Show or hide the TelemFFB panel");
    XPLMRegisterCommandHandler(gToggle, onToggle, 1, nullptr);

    XPLMMenuID plugins = XPLMFindPluginsMenu();
    int item = XPLMAppendMenuItem(plugins, "TelemFFB Panel", nullptr, 0);
    gMenu = XPLMCreateMenu("TelemFFB Panel", plugins, item, nullptr, nullptr);
    XPLMAppendMenuItemWithCommand(gMenu, "Show/Hide Panel", gToggle);
    return 1;
}

PLUGIN_API void XPluginStop() {
    XPLMUnregisterCommandHandler(gToggle, onToggle, 1, nullptr);
    if (gMenu) XPLMDestroyMenu(gMenu);
    gMenu = nullptr;
}

PLUGIN_API int XPluginEnable() {
    gApi = std::make_unique<ApiClient>();
    gApi->start();
    return 1;
}

PLUGIN_API void XPluginDisable() {
    gPanel.reset();
    gApi.reset();
}

PLUGIN_API void XPluginReceiveMessage(XPLMPluginID, int message, void*) {
    if (!gPanel) return;
    if (message == XPLM_MSG_ENTERED_VR) gPanel->setVR(true);
    else if (message == XPLM_MSG_EXITING_VR) gPanel->setVR(false);
}
