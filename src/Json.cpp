// TelemFFB Panel for X-Plane
// Copyright (c) 2026 Micah Frisby
// Licensed under the GNU General Public License v3 - see LICENSE.

#include "Json.h"

#include <clocale>
#include <cstdio>
#include <cstdlib>
#include <locale.h>

namespace {
const Json kNull;
const std::string kEmpty;
constexpr int kMaxDepth = 64;

void appendUtf8(std::string& out, unsigned code) {
    if (code < 0x80) {
        out += static_cast<char>(code);
    } else if (code < 0x800) {
        out += static_cast<char>(0xC0 | (code >> 6));
        out += static_cast<char>(0x80 | (code & 0x3F));
    } else if (code < 0x10000) {
        out += static_cast<char>(0xE0 | (code >> 12));
        out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (code & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (code >> 18));
        out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (code & 0x3F));
    }
}
}  // namespace

class JsonParser {
public:
    explicit JsonParser(const std::string& text) : s_(text) {}

    bool parseDocument(Json& out) {
        skipSpace();
        if (!parseValue(out, 0)) return false;
        skipSpace();
        return pos_ == s_.size();
    }

private:
    const std::string& s_;
    size_t pos_ = 0;

    void skipSpace() {
        while (pos_ < s_.size() && (s_[pos_] == ' ' || s_[pos_] == '\t' || s_[pos_] == '\n' || s_[pos_] == '\r'))
            ++pos_;
    }

    bool literal(const char* word) {
        size_t n = 0;
        while (word[n]) ++n;
        if (s_.compare(pos_, n, word) != 0) return false;
        pos_ += n;
        return true;
    }

    bool parseValue(Json& out, int depth) {
        if (depth > kMaxDepth || pos_ >= s_.size()) return false;
        char c = s_[pos_];
        if (c == '{') return parseObject(out, depth);
        if (c == '[') return parseArray(out, depth);
        if (c == '"') {
            out.type_ = Json::Type::String;
            return parseString(out.string_);
        }
        if (literal("true")) { out.type_ = Json::Type::Bool; out.bool_ = true; return true; }
        if (literal("false")) { out.type_ = Json::Type::Bool; out.bool_ = false; return true; }
        if (literal("null")) { out.type_ = Json::Type::Null; return true; }
        return parseNumber(out);
    }

    bool parseNumber(Json& out) {
        // JSON's decimal point is always '.', whatever locale the sim runs in
        static const _locale_t cLocale = _create_locale(LC_NUMERIC, "C");
        const char* start = s_.c_str() + pos_;
        char* end = nullptr;
        double value = _strtod_l(start, &end, cLocale);
        if (end == start) return false;
        pos_ += static_cast<size_t>(end - start);
        out.type_ = Json::Type::Number;
        out.number_ = value;
        return true;
    }

    bool hex4(unsigned& code) {
        if (pos_ + 4 > s_.size()) return false;
        code = 0;
        for (int i = 0; i < 4; ++i) {
            char h = s_[pos_++];
            code <<= 4;
            if (h >= '0' && h <= '9') code |= h - '0';
            else if (h >= 'a' && h <= 'f') code |= h - 'a' + 10;
            else if (h >= 'A' && h <= 'F') code |= h - 'A' + 10;
            else return false;
        }
        return true;
    }

    bool parseString(std::string& out) {
        ++pos_;  // opening quote
        while (pos_ < s_.size()) {
            char c = s_[pos_++];
            if (c == '"') return true;
            if (c != '\\') { out += c; continue; }
            if (pos_ >= s_.size()) return false;
            char e = s_[pos_++];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    unsigned code;
                    if (!hex4(code)) return false;
                    // a surrogate pair spells one code point above the BMP
                    if (code >= 0xD800 && code <= 0xDBFF && s_.compare(pos_, 2, "\\u") == 0) {
                        pos_ += 2;
                        unsigned low;
                        if (!hex4(low)) return false;
                        code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                    }
                    appendUtf8(out, code);
                    break;
                }
                default: return false;
            }
        }
        return false;
    }

    bool parseArray(Json& out, int depth) {
        out.type_ = Json::Type::Array;
        ++pos_;
        skipSpace();
        if (pos_ < s_.size() && s_[pos_] == ']') { ++pos_; return true; }
        while (true) {
            skipSpace();
            Json item;
            if (!parseValue(item, depth + 1)) return false;
            out.items_.push_back(std::move(item));
            skipSpace();
            if (pos_ >= s_.size()) return false;
            if (s_[pos_] == ',') { ++pos_; continue; }
            if (s_[pos_] == ']') { ++pos_; return true; }
            return false;
        }
    }

    bool parseObject(Json& out, int depth) {
        out.type_ = Json::Type::Object;
        ++pos_;
        skipSpace();
        if (pos_ < s_.size() && s_[pos_] == '}') { ++pos_; return true; }
        while (true) {
            skipSpace();
            if (pos_ >= s_.size() || s_[pos_] != '"') return false;
            std::string key;
            if (!parseString(key)) return false;
            skipSpace();
            if (pos_ >= s_.size() || s_[pos_] != ':') return false;
            ++pos_;
            skipSpace();
            Json value;
            if (!parseValue(value, depth + 1)) return false;
            out.members_.emplace_back(std::move(key), std::move(value));
            skipSpace();
            if (pos_ >= s_.size()) return false;
            if (s_[pos_] == ',') { ++pos_; continue; }
            if (s_[pos_] == '}') { ++pos_; return true; }
            return false;
        }
    }
};

Json Json::parse(const std::string& text, bool* ok) {
    Json result;
    JsonParser parser(text);
    bool parsed = parser.parseDocument(result);
    if (!parsed) result = Json();
    if (ok) *ok = parsed;
    return result;
}

std::string Json::quote(const std::string& text) {
    std::string out = "\"";
    for (unsigned char c : text) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    return out + "\"";
}

bool Json::asBool(bool fallback) const {
    return type_ == Type::Bool ? bool_ : fallback;
}

double Json::asNumber(double fallback) const {
    return type_ == Type::Number ? number_ : fallback;
}

int Json::asInt(int fallback) const {
    return type_ == Type::Number ? static_cast<int>(number_) : fallback;
}

const std::string& Json::asString() const {
    return type_ == Type::String ? string_ : kEmpty;
}

size_t Json::size() const {
    return type_ == Type::Array ? items_.size() : type_ == Type::Object ? members_.size() : 0;
}

const Json& Json::operator[](size_t index) const& {
    return type_ == Type::Array && index < items_.size() ? items_[index] : kNull;
}

const Json& Json::operator[](const std::string& key) const& {
    if (type_ == Type::Object)
        for (const auto& member : members_)
            if (member.first == key) return member.second;
    return kNull;
}

bool Json::has(const std::string& key) const {
    if (type_ == Type::Object)
        for (const auto& member : members_)
            if (member.first == key) return true;
    return false;
}
