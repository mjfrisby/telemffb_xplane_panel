// TelemFFB Panel for X-Plane
// Copyright (c) 2026 Micah Frisby
// Licensed under the GNU General Public License v3 - see LICENSE.
//
// Talks to TelemFFB's in-sim panel server (the same one the MSFS toolbar
// panel uses) on a thread of its own: X-Plane's API is never called from
// here, and the window only ever copies the latest results out and queues
// the changes it wants made.

#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct PanelStatus {
    bool reachable = false;         // TelemFFB answered
    bool connected = false;         // ...and has a sim and aircraft
    std::string sim;
    std::string aircraft;
    std::string pattern;
    std::string device;             // the device whose settings are shown
    std::vector<std::string> devices;
    std::vector<std::string> errors;
};

struct SettingOption {
    std::string value;
    std::string label;
};

// One setting as the server describes it for a panel control.
struct Setting {
    std::string name;
    std::string label;
    std::string grouping;
    std::string order;
    std::string unit;
    std::string info;
    std::string prereq;
    std::string control;            // "bool", "choice", "range" or "button"
    std::string display;            // a range's "percent" or "number"
    bool erasable = false;

    bool checked = false;           // bool
    std::string choice;             // choice
    std::vector<SettingOption> options;
    double value = 0.0;             // range
    double min = 0.0;
    double max = 1.0;
    double step = 0.01;
    int button = 0;                 // button: its number, 0 when unbound
};

// The desktop Monitor tab's content, every value already formatted by
// TelemFFB as the desktop formats it.
struct MonitorEffect {
    std::string label;
    std::string type;               // the full type name, for hovering
    std::string shape;              // a periodic's waveform file, e.g. "wave-sine.svg"
    std::string letter;             // or the letter everything else gets
    bool hasIntensity = false;      // conditions have none: their gains are shown
    double intensity = 0.0;
    std::string shown;
};

struct MonitorRow {
    std::string key;
    std::string value;
    bool favorite = false;
};

struct MonitorData {
    std::vector<MonitorEffect> effects;
    std::vector<MonitorRow> telemetry;
};

struct BindState {
    std::string name;               // the setting waiting for a press, if any
    int remaining = 0;
};

class ApiClient {
public:
    ApiClient();
    ~ApiClient();

    void start();
    void stop();
    // Polls only while the panel is showing; paused rather than stopped,
    // since stopping waits for the thread and that would stall the sim.
    void setActive(bool active);
    // The monitor is read several times a second, only while it is shown.
    void setMonitorActive(bool active);

    PanelStatus status() const;
    MonitorData monitor() const;
    // The settings, when they changed since ``generation``, which is then
    // moved on; false when the copy held is current.
    bool settings(uint64_t& generation, std::vector<Setting>& out) const;
    BindState bind() const;

    // Changes, made in the order asked, each followed by a fresh read.
    void writeBool(const std::string& name, bool value);
    void writeNumber(const std::string& name, double value, bool integer, const std::string& unit);
    void writeChoice(const std::string& name, const std::string& value);
    void erase(const std::string& name);
    void selectDevice(const std::string& device);
    void startBind(const std::string& name);
    // Star or unstar a telemetry key in the favorites the desktop shares.
    void toggleFavorite(const std::string& key);

private:
    struct Response {
        int code = 0;               // 0 when TelemFFB could not be reached
        std::string body;
    };
    struct Change {
        std::wstring path;
        std::string body;
        bool bind = false;          // a binding starts: watch it
        std::string name;
    };

    void run();
    void queue(Change change);
    void pollStatus();
    void pollSettings();
    void pollBind();
    void pollMonitor();
    Response request(const wchar_t* method, const std::wstring& path, const std::string& body = {});

    void* session_ = nullptr;       // HINTERNET, kept out of the header
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> active_{false};
    std::atomic<bool> monitorActive_{false};
    std::mutex waitMutex_;
    std::condition_variable wake_;
    std::deque<Change> changes_;    // guarded by waitMutex_

    mutable std::mutex mutex_;
    PanelStatus status_;
    std::vector<Setting> settings_;
    uint64_t generation_ = 0;
    BindState bind_;
    MonitorData monitor_;
};
