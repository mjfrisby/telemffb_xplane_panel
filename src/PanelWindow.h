// TelemFFB Panel for X-Plane
// Copyright (c) 2026 Micah Frisby
// Licensed under the GNU General Public License v3 - see LICENSE.
//
// The TelemFFB panel: what the MSFS toolbar panel shows, drawn with ImGui
// from the same API.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ApiClient.h"
#include "ImGuiWindow.h"

class PanelWindow : public ImGuiWindow {
public:
    explicit PanelWindow(ApiClient& api);

    void setVisible(bool visible) override;

protected:
    void buildInterface() override;

private:
    enum class View { Settings, Monitor };

    void refreshSettings();
    void drawHeader(const PanelStatus& status);
    void drawDeviceTabs(const PanelStatus& status);
    void drawViewTabs();
    void drawErrors(const PanelStatus& status);
    void drawMonitor();
    void drawEffectRow(const MonitorEffect& effect, float width, float valueWidth);
    void drawBadge(const MonitorEffect& effect, float x, float y, float size);
    void drawTelemetryRow(const MonitorRow& row, float width);
    void drawSettings();
    void drawRow(Setting& item, Setting* inline_, float width, float controlX);
    void drawControl(Setting& item, float width);
    void drawRange(Setting& item, float width);
    void drawErase(const Setting& item);

    ApiClient& api_;
    // The panel's copy of the settings: edited as the user changes them and
    // replaced whenever TelemFFB sends fresh ones, except for the control
    // being held, which keeps its own value until it is let go.
    std::vector<Setting> settings_;
    uint64_t generation_ = 0;
    std::string holding_;
    View view_ = View::Settings;
    bool favoritesOnly_ = true;
};
