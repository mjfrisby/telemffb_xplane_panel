// TelemFFB Panel for X-Plane
// Copyright (c) 2026 Micah Frisby
// Licensed under the GNU General Public License v3 - see LICENSE.

#include "ApiClient.h"

#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <locale.h>

#include "Json.h"
#include "version.h"

namespace {
using Clock = std::chrono::steady_clock;

const wchar_t* const kHost = L"127.0.0.1";
constexpr INTERNET_PORT kPort = 9873;
constexpr auto kStatusInterval = std::chrono::seconds(2);
constexpr auto kSettingsInterval = std::chrono::seconds(3);
constexpr auto kBindInterval = std::chrono::milliseconds(250);
constexpr auto kMonitorInterval = std::chrono::milliseconds(300);
// Short: TelemFFB is on this machine, and stopping waits out a request.
constexpr int kTimeoutMs = 1500;

std::vector<std::string> strings(const Json& array) {
    std::vector<std::string> out;
    for (const Json& item : array.items()) out.push_back(item.asString());
    return out;
}

// A number as JSON writes it, whatever locale the sim runs in.
std::string number(double value, bool integer) {
    static const _locale_t cLocale = _create_locale(LC_NUMERIC, "C");
    char buf[64];
    if (integer) _snprintf_l(buf, sizeof(buf), "%d", cLocale, static_cast<int>(value));
    else _snprintf_l(buf, sizeof(buf), "%.6g", cLocale, value);
    return buf;
}

Setting parseSetting(const Json& item) {
    Setting s;
    s.name = item["name"].asString();
    s.label = item["displayname"].asString();
    s.grouping = item["grouping"].asString();
    s.order = item["order"].asString();
    s.unit = item["unit"].asString();
    s.info = item["info"].asString();
    s.prereq = item["prereq"].asString();
    s.control = item["control"].asString();
    s.display = item["display"].asString();
    s.erasable = item["erasable"].asBool();
    const Json& value = item["value"];
    if (s.control == "bool") {
        s.checked = value.asBool();
    } else if (s.control == "choice") {
        s.choice = value.asString();
        for (const Json& option : item["options"].items())
            s.options.push_back({option["value"].asString(), option["label"].asString()});
    } else if (s.control == "range") {
        s.value = value.asNumber();
        s.min = item["min"].asNumber(0.0);
        s.max = item["max"].asNumber(1.0);
        s.step = item["step"].asNumber(0.01);
    } else if (s.control == "button") {
        s.button = value.asInt();
    }
    return s;
}
}  // namespace

ApiClient::ApiClient() {
    session_ = WinHttpOpen(L"TelemFFB-Panel/" PANEL_VERSION_W, WINHTTP_ACCESS_TYPE_NO_PROXY,
                           WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (session_) WinHttpSetTimeouts(session_, kTimeoutMs, kTimeoutMs, kTimeoutMs, kTimeoutMs);
}

ApiClient::~ApiClient() {
    stop();
    if (session_) WinHttpCloseHandle(session_);
}

void ApiClient::start() {
    if (running_.exchange(true)) return;
    thread_ = std::thread(&ApiClient::run, this);
}

void ApiClient::stop() {
    {
        // under the lock the thread waits on, so the wake cannot fall
        // between its check and its wait
        std::lock_guard<std::mutex> lock(waitMutex_);
        if (!running_.exchange(false)) return;
    }
    wake_.notify_all();
    if (thread_.joinable()) thread_.join();
}

void ApiClient::setActive(bool active) {
    {
        std::lock_guard<std::mutex> lock(waitMutex_);
        active_ = active;
    }
    if (active) wake_.notify_all();
}

void ApiClient::setMonitorActive(bool active) {
    {
        std::lock_guard<std::mutex> lock(waitMutex_);
        monitorActive_ = active;
    }
    if (active) wake_.notify_all();
}

MonitorData ApiClient::monitor() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return monitor_;
}

PanelStatus ApiClient::status() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return status_;
}

bool ApiClient::settings(uint64_t& generation, std::vector<Setting>& out) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (generation == generation_) return false;
    generation = generation_;
    out = settings_;
    return true;
}

BindState ApiClient::bind() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return bind_;
}

void ApiClient::writeBool(const std::string& name, bool value) {
    queue({L"/api/settings", "{\"name\":" + Json::quote(name) + ",\"value\":" + (value ? "true" : "false") + "}"});
}

void ApiClient::writeNumber(const std::string& name, double value, bool integer, const std::string& unit) {
    queue({L"/api/settings", "{\"name\":" + Json::quote(name) + ",\"value\":" + number(value, integer) +
                                 ",\"unit\":" + Json::quote(unit) + "}"});
}

void ApiClient::writeChoice(const std::string& name, const std::string& value) {
    queue({L"/api/settings", "{\"name\":" + Json::quote(name) + ",\"value\":" + Json::quote(value) + "}"});
}

void ApiClient::erase(const std::string& name) {
    queue({L"/api/erase", "{\"name\":" + Json::quote(name) + "}"});
}

void ApiClient::selectDevice(const std::string& device) {
    queue({L"/api/device", "{\"device\":" + Json::quote(device) + "}"});
}

void ApiClient::startBind(const std::string& name) {
    {
        // shown as waiting at once, not after the request goes out
        std::lock_guard<std::mutex> lock(mutex_);
        if (!bind_.name.empty()) return;    // one capture at a time
        bind_.name = name;
        bind_.remaining = 5;
    }
    queue({L"/api/bind", "{\"name\":" + Json::quote(name) + "}", true, name});
}

void ApiClient::toggleFavorite(const std::string& key) {
    {
        // shown starred at once, not a poll later
        std::lock_guard<std::mutex> lock(mutex_);
        for (MonitorRow& row : monitor_.telemetry)
            if (row.key == key) row.favorite = !row.favorite;
    }
    queue({L"/api/monitor/favorite", "{\"key\":" + Json::quote(key) + "}"});
}

void ApiClient::queue(Change change) {
    {
        std::lock_guard<std::mutex> lock(waitMutex_);
        changes_.push_back(std::move(change));
    }
    wake_.notify_all();
}

void ApiClient::run() {
    auto nextStatus = Clock::now();
    auto nextSettings = Clock::now();
    auto nextMonitor = Clock::now();
    while (running_) {
        std::deque<Change> changes;
        {
            std::unique_lock<std::mutex> lock(waitMutex_);
            if (!active_) {
                wake_.wait(lock, [this] { return !running_ || active_; });
                nextStatus = nextSettings = nextMonitor = Clock::now();
                continue;
            }
            changes.swap(changes_);
        }

        for (const Change& change : changes) {
            Response response = request(L"POST", change.path, change.body);
            if (change.bind && response.code != 200) {
                std::lock_guard<std::mutex> lock(mutex_);
                bind_ = BindState();        // refused: nothing is waiting
            }
        }
        if (!changes.empty()) nextStatus = nextSettings = nextMonitor = Clock::now();

        bool binding;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            binding = !bind_.name.empty();
        }
        if (binding) pollBind();

        const auto now = Clock::now();
        if (now >= nextStatus) {
            pollStatus();
            nextStatus = now + kStatusInterval;
        }
        if (now >= nextSettings) {
            pollSettings();
            nextSettings = now + kSettingsInterval;
        }
        const bool monitoring = monitorActive_;
        if (monitoring && now >= nextMonitor) {
            pollMonitor();
            nextMonitor = now + kMonitorInterval;
        }

        auto until = std::min(nextStatus, nextSettings);
        if (monitoring) until = std::min(until, nextMonitor);
        if (binding) until = std::min(until, Clock::now() + kBindInterval);
        std::unique_lock<std::mutex> lock(waitMutex_);
        wake_.wait_until(lock, until, [this, monitoring] {
            return !running_ || !changes_.empty() || (monitorActive_ && !monitoring);
        });
    }
}

void ApiClient::pollStatus() {
    PanelStatus next;
    Response response = request(L"GET", L"/api/status");
    if (response.code == 200) {
        Json json = Json::parse(response.body);
        next.reachable = true;
        next.connected = json["connected"].asBool();
        next.sim = json["sim"].asString();
        next.aircraft = json["aircraft"].asString();
        next.pattern = json["pattern"].asString();
        next.device = json["device"].asString();
        next.devices = strings(json["devices"]);
        next.errors = strings(json["errors"]);
    }
    std::lock_guard<std::mutex> lock(mutex_);
    status_ = std::move(next);
}

void ApiClient::pollSettings() {
    Response response = request(L"GET", L"/api/settings");
    if (response.code != 200) return;
    // held in a variable: a range-for keeps only its outermost temporary
    // alive, and the items belong to the parsed document
    const Json json = Json::parse(response.body);
    std::vector<Setting> next;
    for (const Json& item : json["settings"].items()) next.push_back(parseSetting(item));
    std::lock_guard<std::mutex> lock(mutex_);
    settings_ = std::move(next);
    ++generation_;
}

void ApiClient::pollBind() {
    Response response = request(L"GET", L"/api/bind");
    Json json = Json::parse(response.body);
    const bool waiting = response.code == 200 && json["state"].asString() == "waiting";
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (waiting) {
            bind_.remaining = json["remaining"].asInt(bind_.remaining);
            return;
        }
        bind_ = BindState();
    }
    pollSettings();     // captured or timed out: show the binding as it now is
}

void ApiClient::pollMonitor() {
    Response response = request(L"GET", L"/api/monitor");
    if (response.code != 200) return;
    const Json json = Json::parse(response.body);
    MonitorData next;
    for (const Json& item : json["effects"].items()) {
        MonitorEffect effect;
        effect.label = item["label"].asString();
        effect.type = item["type"].asString();
        effect.shape = item["shape"].asString();
        effect.letter = item["letter"].asString();
        effect.hasIntensity = !item["intensity"].isNull();
        effect.intensity = item["intensity"].asNumber();
        effect.shown = item["shown"].asString();
        next.effects.push_back(std::move(effect));
    }
    for (const Json& item : json["telemetry"].items())
        next.telemetry.push_back({item["key"].asString(), item["value"].asString(), item["favorite"].asBool()});
    std::lock_guard<std::mutex> lock(mutex_);
    monitor_ = std::move(next);
}

ApiClient::Response ApiClient::request(const wchar_t* method, const std::wstring& path, const std::string& body) {
    Response response;
    if (!session_) return response;
    HINTERNET connection = WinHttpConnect(session_, kHost, kPort, 0);
    if (!connection) return response;
    HINTERNET req = WinHttpOpenRequest(connection, method, path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                       WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
    if (req) {
        const bool hasBody = !body.empty();
        BOOL sent = WinHttpSendRequest(
            req,
            hasBody ? L"Content-Type: application/json\r\n" : WINHTTP_NO_ADDITIONAL_HEADERS,
            hasBody ? static_cast<DWORD>(-1L) : 0,
            hasBody ? const_cast<char*>(body.data()) : WINHTTP_NO_REQUEST_DATA,
            static_cast<DWORD>(body.size()), static_cast<DWORD>(body.size()), 0);
        if (sent && WinHttpReceiveResponse(req, nullptr)) {
            DWORD code = 0;
            DWORD size = sizeof(code);
            WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &code, &size, WINHTTP_NO_HEADER_INDEX);
            response.code = static_cast<int>(code);
            DWORD available = 0;
            while (WinHttpQueryDataAvailable(req, &available) && available > 0) {
                std::string chunk(available, '\0');
                DWORD read = 0;
                if (!WinHttpReadData(req, chunk.data(), available, &read) || read == 0) break;
                response.body.append(chunk.data(), read);
            }
        }
        WinHttpCloseHandle(req);
    }
    WinHttpCloseHandle(connection);
    return response;
}
