// TelemFFB Panel for X-Plane
// Copyright (c) 2026 Micah Frisby
// Licensed under the GNU General Public License v3 - see LICENSE.
//
// A small JSON reader for TelemFFB's panel API: parse a response, read it
// with fallbacks for anything missing, and quote strings for requests.

#pragma once

#include <string>
#include <utility>
#include <vector>

class Json {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    // Parse ``text``; a malformed document reads as null, with ``ok`` false.
    static Json parse(const std::string& text, bool* ok = nullptr);

    // ``text`` as a JSON string literal, quotes included.
    static std::string quote(const std::string& text);

    Type type() const { return type_; }
    bool isNull() const { return type_ == Type::Null; }
    bool isObject() const { return type_ == Type::Object; }
    bool isArray() const { return type_ == Type::Array; }

    bool asBool(bool fallback = false) const;
    double asNumber(double fallback = 0.0) const;
    int asInt(int fallback = 0) const;
    // The string, or empty for anything that is not one.
    const std::string& asString() const;

    // Array elements, or object members' count.
    size_t size() const;
    bool has(const std::string& key) const;

    // References into this value, so not taken from a temporary: in
    // ``for (x : Json::parse(s)["k"].items())`` the parsed document is gone
    // before the loop starts. Parse into a variable first.
    const Json& operator[](size_t index) const&;
    const Json& operator[](const std::string& key) const&;
    const std::vector<Json>& items() const& { return items_; }
    const std::vector<std::pair<std::string, Json>>& members() const& { return members_; }
    const Json& operator[](size_t index) const&& = delete;
    const Json& operator[](const std::string& key) const&& = delete;
    const std::vector<Json>& items() const&& = delete;
    const std::vector<std::pair<std::string, Json>>& members() const&& = delete;

private:
    friend class JsonParser;

    Type type_ = Type::Null;
    bool bool_ = false;
    double number_ = 0.0;
    std::string string_;
    std::vector<Json> items_;
    std::vector<std::pair<std::string, Json>> members_;
};
