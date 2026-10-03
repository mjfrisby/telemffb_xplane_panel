// TelemFFB Panel for X-Plane
// Copyright (c) 2026 Micah Frisby
// Licensed under the GNU General Public License v3 - see LICENSE.

#include "PanelWindow.h"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <locale.h>
#include <map>

#include "imgui.h"
#include "version.h"

namespace {
const ImVec4 kTitle(0.725f, 0.533f, 1.0f, 1.0f);      // #b988ff
const ImVec4 kMuted(0.604f, 0.627f, 0.675f, 1.0f);    // #9aa0ac
const ImVec4 kError(0.953f, 0.788f, 0.808f, 1.0f);    // #f3c9ce
const ImVec4 kErrorBg(0.227f, 0.122f, 0.141f, 1.0f);  // #3a1f24
const ImVec4 kPurple(0.416f, 0.184f, 0.690f, 1.0f);   // #6a2fb0
const ImU32 kEraseFill = IM_COL32(0x6a, 0x2f, 0xb0, 0xff);
const ImU32 kBadge = IM_COL32(0xc4, 0x73, 0xd9, 0xff);        // the desktop's badge color on a dark theme
const ImU32 kBarTrack = IM_COL32(0x2a, 0x2e, 0x38, 0xff);
const ImU32 kBarFill = IM_COL32(0x6a, 0x2f, 0xb0, 0xff);
const ImU32 kRowLine = IM_COL32(0x22, 0x25, 0x2c, 0xff);
const ImU32 kStarOn = IM_COL32(0xf5, 0xc5, 0x18, 0xff);       // the desktop's gold
const ImU32 kStarEdge = IM_COL32(0xb8, 0x92, 0x0f, 0xff);
const ImU32 kStarOff = IM_COL32(0x6c, 0x72, 0x80, 0xff);
const ImU32 kStarHover = IM_COL32(0x9a, 0xa0, 0xac, 0xff);
constexpr float kPi = 3.14159265f;
const ImU32 kEraseHover = IM_COL32(0x8b, 0x4f, 0xd0, 0xff);

// A setting's order as a number: "2100.74" sorts after "2100.6".
double orderValue(const std::string& order) {
    static const _locale_t cLocale = _create_locale(LC_NUMERIC, "C");
    if (order.empty()) return 1e18;
    char* end = nullptr;
    double value = _strtod_l(order.c_str(), &end, cLocale);
    return end == order.c_str() ? 1e18 : value;
}

// The digits after the order's point, as a number: .1 marks a setting shown
// on its parent's row, .2 and up an indented child (as the desktop form).
int decimalDigits(const std::string& order) {
    size_t dot = order.find('.');
    if (dot == std::string::npos || dot + 1 >= order.size()) return -1;
    return std::atoi(order.c_str() + dot + 1);
}

bool isInline(const std::string& order) {
    return order.find('.') != std::string::npos && !order.empty() && order.back() == '1';
}

const char* deviceLabel(const std::string& device) {
    if (device == "joystick") return "Joystick";
    if (device == "pedals") return "Pedals";
    if (device == "collective") return "Collective";
    if (device == "trimwheel") return "Trim Wheel";
    return device.c_str();
}

// An ImGui format string showing ``unit`` after the number: '%' escaped.
std::string withUnit(const char* number, const std::string& unit) {
    std::string out = number;
    for (char c : unit) {
        if (c == '%') out += "%%";
        else out += c;
    }
    return out;
}

// A row of buttons working as tabs, the selected one in purple; true when
// an unselected one is pressed.
bool tab(const char* label, bool selected) {
    if (selected) ImGui::PushStyleColor(ImGuiCol_Button, kPurple);
    const bool pressed = ImGui::Button(label);
    if (selected) ImGui::PopStyleColor();
    ImGui::SameLine();
    return pressed && !selected;
}

void sectionTitle(const std::string& text) {
    std::string title = text;
    std::transform(title.begin(), title.end(), title.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    ImGui::Spacing();
    ImGui::TextColored(kTitle, "%s", title.c_str());
    ImGui::Separator();
}

void tooltip(const std::string& text) {
    if (!text.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 24.0f);
        ImGui::TextUnformatted(text.c_str());
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}
}  // namespace

PanelWindow::PanelWindow(ApiClient& api) : ImGuiWindow("TelemFFB", 460, 620), api_(api) {}

void PanelWindow::setVisible(bool visible) {
    ImGuiWindow::setVisible(visible);
    api_.setActive(visible);
    api_.setMonitorActive(visible && view_ == View::Monitor);
}

void PanelWindow::refreshSettings() {
    std::vector<Setting> fresh;
    if (!api_.settings(generation_, fresh)) return;
    if (!holding_.empty()) {
        auto held = std::find_if(settings_.begin(), settings_.end(),
                                 [this](const Setting& s) { return s.name == holding_; });
        if (held != settings_.end())
            for (Setting& s : fresh)
                if (s.name == holding_) s = *held;
    }
    settings_ = std::move(fresh);
}

void PanelWindow::buildInterface() {
    const PanelStatus status = api_.status();
    refreshSettings();

    drawHeader(status);
    if (!status.reachable) {
        ImGui::TextWrapped("TelemFFB is not running, or its in-sim panel is turned off in System Settings.");
        return;
    }
    if (!status.connected) {
        ImGui::TextWrapped("Waiting for TelemFFB to see an aircraft...");
        return;
    }
    drawDeviceTabs(status);
    drawViewTabs();
    drawErrors(status);
    ImGui::Spacing();

    ImGui::BeginChild(view_ == View::Settings ? "##settings" : "##monitor", ImVec2(0.0f, 0.0f));
    if (view_ == View::Settings) drawSettings();
    else drawMonitor();
    ImGui::EndChild();
}

void PanelWindow::drawViewTabs() {
    View next = view_;
    if (tab("Settings", view_ == View::Settings)) next = View::Settings;
    if (tab("Monitor", view_ == View::Monitor)) next = View::Monitor;
    ImGui::NewLine();
    if (next != view_) {
        view_ = next;
        api_.setMonitorActive(view_ == View::Monitor);
    }
}

void PanelWindow::drawMonitor() {
    const MonitorData data = api_.monitor();
    if (tab("Favorites", favoritesOnly_)) favoritesOnly_ = true;
    if (tab("All", !favoritesOnly_)) favoritesOnly_ = false;
    ImGui::NewLine();

    const float width = ImGui::GetContentRegionAvail().x;
    ImFont* mono = monoFont() ? monoFont() : ImGui::GetFont();
    // one value column for every row, so the bars line up
    const float valueWidth = mono->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, 0.0f, "X 100% Y 100%").x;

    sectionTitle("Active Effects");
    if (data.effects.empty()) ImGui::TextColored(kMuted, "No effects are active.");
    for (const MonitorEffect& effect : data.effects) drawEffectRow(effect, width, valueWidth);

    sectionTitle("Telemetry");
    std::vector<const MonitorRow*> rows;
    for (const MonitorRow& row : data.telemetry)
        if (!favoritesOnly_ || row.favorite) rows.push_back(&row);
    if (rows.empty()) {
        ImGui::TextColored(kMuted, data.telemetry.empty() ? "Waiting for telemetry..."
                                                          : "No favorites yet: show All and tap a star to add one.");
        return;
    }
    // only the rows in view are laid out: the full list runs past a hundred
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(rows.size()));
    while (clipper.Step())
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) drawTelemetryRow(*rows[i], width);
}

void PanelWindow::drawEffectRow(const MonitorEffect& effect, float width, float valueWidth) {
    const float height = ImGui::GetFrameHeight();
    const float size = ImGui::GetFontSize();
    const float gap = ImGui::GetStyle().ItemSpacing.x;
    const ImVec2 at = ImGui::GetCursorScreenPos();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImFont* mono = monoFont() ? monoFont() : ImGui::GetFont();
    const float textY = at.y + (height - size) * 0.5f;

    drawBadge(effect, at.x, at.y, height);

    const float valueX = at.x + width - valueWidth;
    const float barWidth = std::min(70.0f, width * 0.2f);
    const float barX = valueX - gap - barWidth;
    const float labelX = at.x + height + gap;
    draw->PushClipRect(ImVec2(labelX, at.y), ImVec2(barX - gap, at.y + height), true);
    draw->AddText(ImVec2(labelX, textY), ImGui::GetColorU32(ImGuiCol_Text), effect.label.c_str());
    draw->PopClipRect();

    if (effect.hasIntensity) {
        const float barHeight = 8.0f;
        const float barY = at.y + (height - barHeight) * 0.5f;
        const float fill = std::min(1.0f, std::max(0.0f, static_cast<float>(effect.intensity)));
        draw->AddRectFilled(ImVec2(barX, barY), ImVec2(barX + barWidth, barY + barHeight), kBarTrack, 4.0f);
        if (fill > 0.0f)
            draw->AddRectFilled(ImVec2(barX, barY), ImVec2(barX + barWidth * fill, barY + barHeight), kBarFill, 4.0f);
    }
    const float shownWidth = mono->CalcTextSizeA(size, FLT_MAX, 0.0f, effect.shown.c_str()).x;
    draw->AddText(mono, size, ImVec2(at.x + width - shownWidth, textY), ImGui::GetColorU32(ImGuiCol_Text),
                  effect.shown.c_str());
    draw->AddLine(ImVec2(at.x, at.y + height), ImVec2(at.x + width, at.y + height), kRowLine);

    ImGui::Dummy(ImVec2(width, height));
    if (!effect.type.empty() && ImGui::IsMouseHoveringRect(at, ImVec2(at.x + height, at.y + height)))
        ImGui::SetTooltip("%s", effect.type.c_str());
}

void PanelWindow::drawBadge(const MonitorEffect& effect, float x, float y, float size) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const float pad = size * 0.2f;
    const float x0 = x + pad, x1 = x + size - pad, y0 = y + pad, y1 = y + size - pad;
    const float w = x1 - x0, mid = (y0 + y1) * 0.5f, half = (y1 - y0) * 0.5f;
    const float thickness = std::max(1.5f, size * 0.09f);

    std::vector<ImVec2> line;
    if (effect.shape == "wave-sine.svg") {
        for (int i = 0; i <= 24; ++i) {
            const float t = i / 24.0f;
            line.push_back(ImVec2(x0 + w * t, mid - std::sin(2.0f * kPi * t) * half));
        }
    } else if (effect.shape == "wave-square.svg") {
        line = {{x0, y1}, {x0 + w * 0.25f, y1}, {x0 + w * 0.25f, y0}, {x0 + w * 0.75f, y0},
                {x0 + w * 0.75f, y1}, {x1, y1}};
    } else if (effect.shape == "wave-triangle.svg") {
        line = {{x0, mid}, {x0 + w * 0.25f, y0}, {x0 + w * 0.75f, y1}, {x1, mid}};
    } else if (effect.shape == "wave-sawtooth-up.svg") {
        line = {{x0, y1}, {x0 + w * 0.5f, y0}, {x0 + w * 0.5f, y1}, {x1, y0}};
    } else if (effect.shape == "wave-sawtooth-down.svg") {
        line = {{x0, y0}, {x0 + w * 0.5f, y1}, {x0 + w * 0.5f, y0}, {x1, y1}};
    }
    if (!line.empty()) {
        draw->AddPolyline(line.data(), static_cast<int>(line.size()), kBadge, 0, thickness);
        return;
    }
    if (effect.letter.empty()) return;
    // bold monospace at 1.4x, as the desktop sets it: monospace serifs the I
    ImFont* font = monoBoldFont() ? monoBoldFont() : ImGui::GetFont();
    const float letterSize = ImGui::GetFontSize() * 1.4f;
    const ImVec2 extent = font->CalcTextSizeA(letterSize, FLT_MAX, 0.0f, effect.letter.c_str());
    draw->AddText(font, letterSize, ImVec2(x + (size - extent.x) * 0.5f, y + (size - extent.y) * 0.5f), kBadge,
                  effect.letter.c_str());
}

void PanelWindow::drawTelemetryRow(const MonitorRow& row, float width) {
    const float height = ImGui::GetFrameHeight();
    const float size = ImGui::GetFontSize();
    const float gap = ImGui::GetStyle().ItemSpacing.x;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImFont* mono = monoFont() ? monoFont() : ImGui::GetFont();

    ImGui::PushID(row.key.c_str());
    const ImVec2 at = ImGui::GetCursorScreenPos();
    if (ImGui::InvisibleButton("##star", ImVec2(height, height))) api_.toggleFavorite(row.key);
    const bool hovered = ImGui::IsItemHovered();
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::Dummy(ImVec2(width - height, height));
    ImGui::PopID();

    // the star: five points, filled gold when starred
    const ImVec2 center(at.x + height * 0.5f, at.y + height * 0.5f);
    const float outer = height * 0.36f;
    ImVec2 points[10];
    for (int i = 0; i < 10; ++i) {
        const float angle = -kPi * 0.5f + i * kPi / 5.0f;
        const float r = (i % 2 == 0) ? outer : outer * 0.45f;
        points[i] = ImVec2(center.x + std::cos(angle) * r, center.y + std::sin(angle) * r);
    }
    if (row.favorite) {
        draw->AddConcavePolyFilled(points, 10, kStarOn);
        draw->AddPolyline(points, 10, kStarEdge, ImDrawFlags_Closed, 1.0f);
    } else {
        draw->AddPolyline(points, 10, hovered ? kStarHover : kStarOff, ImDrawFlags_Closed, 1.4f);
    }

    const float textY = at.y + (height - size) * 0.5f;
    const float valueWidth = mono->CalcTextSizeA(size, FLT_MAX, 0.0f, row.value.c_str()).x;
    const float valueX = std::max(at.x + width * 0.45f, at.x + width - valueWidth);
    const float keyX = at.x + height + gap;
    draw->PushClipRect(ImVec2(keyX, at.y), ImVec2(valueX - gap, at.y + height), true);
    draw->AddText(ImVec2(keyX, textY), ImGui::GetColorU32(ImGuiCol_Text), row.key.c_str());
    draw->PopClipRect();
    draw->PushClipRect(ImVec2(valueX, at.y), ImVec2(at.x + width, at.y + height), true);
    draw->AddText(mono, size, ImVec2(valueX, textY), ImGui::GetColorU32(ImGuiCol_Text), row.value.c_str());
    draw->PopClipRect();
    draw->AddLine(ImVec2(at.x, at.y + height), ImVec2(at.x + width, at.y + height), kRowLine);
}

void PanelWindow::drawHeader(const PanelStatus& status) {
    ImGui::TextColored(kTitle, "TelemFFB");
    ImGui::SameLine();
    // the panel's own version, not TelemFFB's
    ImGui::TextColored(kMuted, "X-Plane Panel v" PANEL_VERSION);
    if (status.connected) {
        ImGui::TextUnformatted(status.pattern.empty() ? status.aircraft.c_str() : status.pattern.c_str());
        if (status.devices.size() < 2) {
            ImGui::SameLine();
            ImGui::TextColored(kMuted, "- %s", status.device.c_str());
        }
    }
    ImGui::Separator();
}

void PanelWindow::drawDeviceTabs(const PanelStatus& status) {
    if (status.devices.size() < 2) return;
    for (const std::string& device : status.devices) {
        const bool selected = device == status.device;
        if (selected) ImGui::PushStyleColor(ImGuiCol_Button, kPurple);
        if (ImGui::Button(deviceLabel(device)) && !selected) api_.selectDevice(device);
        if (selected) ImGui::PopStyleColor();
        ImGui::SameLine();
    }
    ImGui::NewLine();
}

void PanelWindow::drawErrors(const PanelStatus& status) {
    // The configuration errors TelemFFB is showing, as it shows and clears them
    for (const std::string& error : status.errors) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_ChildBg, kErrorBg);
        ImGui::PushStyleColor(ImGuiCol_Text, kError);
        ImGui::BeginChild(ImGui::GetID(error.c_str()), ImVec2(0, 0),
                          ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_FrameStyle);
        ImGui::TextWrapped("%s", error.c_str());
        ImGui::EndChild();
        ImGui::PopStyleColor(2);
    }
}

void PanelWindow::drawSettings() {
    if (settings_.empty()) {
        ImGui::TextColored(kMuted, "No settings to show for this aircraft yet.");
        return;
    }

    // Settings shown on their parent's row, by the parent's name
    std::map<std::string, Setting*> inlined;
    std::map<std::string, bool> present;
    for (const Setting& s : settings_) present[s.name] = true;
    std::vector<Setting*> rows;
    for (Setting& s : settings_) {
        if (isInline(s.order) && present.count(s.prereq) && !inlined.count(s.prereq)) inlined[s.prereq] = &s;
        else rows.push_back(&s);
    }

    // Groups, in the order of each one's first setting
    std::vector<std::pair<std::string, std::vector<Setting*>>> groups;
    for (Setting* s : rows) {
        const std::string key = s->grouping.empty() ? "Settings" : s->grouping;
        auto group = std::find_if(groups.begin(), groups.end(), [&](const auto& g) { return g.first == key; });
        if (group == groups.end()) {
            groups.push_back({key, {}});
            group = groups.end() - 1;
        }
        group->second.push_back(s);
    }
    for (auto& group : groups)
        std::stable_sort(group.second.begin(), group.second.end(),
                         [](const Setting* a, const Setting* b) { return orderValue(a->order) < orderValue(b->order); });
    std::stable_sort(groups.begin(), groups.end(), [](const auto& a, const auto& b) {
        return orderValue(a.second.front()->order) < orderValue(b.second.front()->order);
    });

    const float width = ImGui::GetContentRegionAvail().x;
    const float controlX = std::max(140.0f, width * 0.48f);
    for (auto& group : groups) {
        ImGui::Spacing();
        std::string title = group.first;
        std::transform(title.begin(), title.end(), title.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        ImGui::TextColored(kTitle, "%s", title.c_str());
        ImGui::Separator();
        for (Setting* s : group.second) {
            auto found = inlined.find(s->name);
            drawRow(*s, found == inlined.end() ? nullptr : found->second, width, controlX);
        }
    }
}

void PanelWindow::drawRow(Setting& item, Setting* inline_, float width, float controlX) {
    const float erase = ImGui::GetFrameHeight();
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const bool indented = decimalDigits(item.order) >= 2;
    const float indent = indented ? ImGui::GetFontSize() : 0.0f;

    ImGui::PushID(item.name.c_str());
    ImGui::AlignTextToFramePadding();
    // the label, clipped short of the controls it sits beside
    const ImVec2 at = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(ImVec2(at.x + indent, at.y));
    ImGui::PushClipRect(ImVec2(at.x, at.y), ImVec2(at.x + controlX - spacing, at.y + ImGui::GetFrameHeight()), true);
    ImGui::TextUnformatted(item.label.c_str());
    ImGui::PopClipRect();
    tooltip(item.info);

    // each control is followed by its own erase slot, the last at the right edge
    const float controls = width - controlX - erase - spacing;
    ImGui::SameLine(controlX);
    if (inline_) {
        const float half = (controls - erase - 2.0f * spacing) / 2.0f;
        drawControl(item, half);
        ImGui::SameLine();
        drawErase(item);
        ImGui::SameLine();
        ImGui::PushID(inline_->name.c_str());
        drawControl(*inline_, half);
        ImGui::SameLine(width - erase);
        drawErase(*inline_);
        ImGui::PopID();
    } else {
        drawControl(item, controls);
        ImGui::SameLine(width - erase);
        drawErase(item);
    }
    ImGui::PopID();
}

void PanelWindow::drawControl(Setting& item, float width) {
    if (item.control == "bool") {
        if (ImGui::Checkbox("##value", &item.checked)) api_.writeBool(item.name, item.checked);
    } else if (item.control == "choice") {
        const char* shown = item.choice.c_str();
        for (const SettingOption& option : item.options)
            if (option.value == item.choice) shown = option.label.c_str();
        ImGui::SetNextItemWidth(width);
        if (ImGui::BeginCombo("##value", shown)) {
            for (const SettingOption& option : item.options) {
                const bool selected = option.value == item.choice;
                if (ImGui::Selectable(option.label.c_str(), selected) && !selected) {
                    item.choice = option.value;
                    api_.writeChoice(item.name, option.value);
                }
            }
            ImGui::EndCombo();
        }
    } else if (item.control == "range") {
        drawRange(item, width);
    } else if (item.control == "button") {
        const BindState bind = api_.bind();
        char label[128];
        if (bind.name == item.name) std::snprintf(label, sizeof(label), "Push a button... %d", bind.remaining);
        else if (!item.buttonLabel.empty()) std::snprintf(label, sizeof(label), "%s", item.buttonLabel.c_str());
        else if (item.button) std::snprintf(label, sizeof(label), "Button %d", item.button);
        else std::snprintf(label, sizeof(label), "Click to bind");
        if (ImGui::Button(label, ImVec2(width, 0.0f)) && bind.name.empty()) api_.startBind(item.name);
    }
    tooltip(item.info);
}

void PanelWindow::drawRange(Setting& item, float width) {
    const bool percent = item.display == "percent";
    const bool integer = !percent && item.step >= 1.0 && std::floor(item.step) == item.step;
    const float scale = percent ? 100.0f : 1.0f;
    float shown = static_cast<float>(item.value) * scale;
    const std::string format = percent ? "%.1f%%" : withUnit(integer ? "%.0f" : "%.2f", item.unit);

    ImGui::SetNextItemWidth(width);
    // no Ctrl-click typing: the keyboard stays with X-Plane
    if (ImGui::SliderFloat("##value", &shown, static_cast<float>(item.min) * scale,
                           static_cast<float>(item.max) * scale, format.c_str(), ImGuiSliderFlags_NoInput)) {
        double value = shown / scale;
        if (item.step > 0.0) value = item.min + std::round((value - item.min) / item.step) * item.step;
        item.value = std::min(item.max, std::max(item.min, value));
    }
    if (ImGui::IsItemActivated()) holding_ = item.name;
    if (ImGui::IsItemDeactivated()) {
        if (ImGui::IsItemDeactivatedAfterEdit()) api_.writeNumber(item.name, item.value, integer, item.unit);
        holding_.clear();
    }
}

void PanelWindow::drawErase(const Setting& item) {
    const float size = ImGui::GetFrameHeight();
    if (!item.erasable) {
        ImGui::Dummy(ImVec2(size, size));
        return;
    }
    const ImVec2 at = ImGui::GetCursorScreenPos();
    if (ImGui::InvisibleButton("##erase", ImVec2(size, size))) api_.erase(item.name);
    const bool hovered = ImGui::IsItemHovered();
    if (hovered) ImGui::SetTooltip("Reset to default");
    // the desktop form's erase button: a purple disc with a white X
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 center(at.x + size * 0.5f, at.y + size * 0.5f);
    const float radius = size * 0.42f;
    const float arm = radius * 0.42f;
    draw->AddCircleFilled(center, radius, hovered ? kEraseHover : kEraseFill);
    draw->AddLine(ImVec2(center.x - arm, center.y - arm), ImVec2(center.x + arm, center.y + arm), IM_COL32_WHITE, 2.0f);
    draw->AddLine(ImVec2(center.x + arm, center.y - arm), ImVec2(center.x - arm, center.y + arm), IM_COL32_WHITE, 2.0f);
}
