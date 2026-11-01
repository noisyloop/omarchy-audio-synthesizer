#include "sg/json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>

namespace sg {

const Json& Json::operator[](std::size_t i) const noexcept {
    static const Json null;
    return type_ == Type::Array && i < arr_.size() ? arr_[i] : null;
}

const Json& Json::operator[](std::string_view key) const noexcept {
    static const Json null;
    if (type_ == Type::Object)
        for (const auto& [k, v] : obj_) if (k == key) return v;
    return null;
}

bool Json::has(std::string_view key) const noexcept {
    if (type_ == Type::Object)
        for (const auto& kv : obj_) if (kv.first == key) return true;
    return false;
}

Json& Json::set(std::string key, Json v) {
    type_ = Type::Object;
    for (auto& kv : obj_)
        if (kv.first == key) { kv.second = std::move(v); return kv.second; }
    obj_.emplace_back(std::move(key), std::move(v));
    return obj_.back().second;
}

struct JsonParser {
    std::string_view s;
    std::size_t i = 0;
    int depth = 0;

    [[noreturn]] void fail(const char* what) const { throw std::runtime_error(std::string("JSON: ") + what); }
    void ws() { while (i < s.size() && (s[i] == ' ' || s[i] == '\n' || s[i] == '\r' || s[i] == '\t')) i++; }
    bool eat(char c) { ws(); if (i < s.size() && s[i] == c) { i++; return true; } return false; }

    static void utf8(std::string& out, unsigned cp) {
        if (cp < 0x80) out += static_cast<char>(cp);
        else if (cp < 0x800) { out += static_cast<char>(0xC0 | (cp >> 6)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { out += static_cast<char>(0xE0 | (cp >> 12)); out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
        else { out += static_cast<char>(0xF0 | (cp >> 18)); out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F)); out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
    }
    unsigned hex4() {
        if (i + 4 > s.size()) fail("bad escape");
        unsigned v = 0;
        for (int k = 0; k < 4; k++) {
            const char c = s[i++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') v |= static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= static_cast<unsigned>(c - 'A' + 10);
            else fail("bad escape");
        }
        return v;
    }
    std::string str() {
        if (!eat('"')) fail("expected string");
        std::string out;
        while (true) {
            if (i >= s.size()) fail("unterminated string");
            const char c = s[i++];
            if (c == '"') break;
            if (static_cast<unsigned char>(c) < 0x20) fail("control character in string");
            if (c != '\\') { out += c; continue; }
            if (i >= s.size()) fail("bad escape");
            const char e = s[i++];
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
                unsigned cp = hex4();
                if (cp >= 0xD800 && cp < 0xDC00 && i + 6 <= s.size() && s[i] == '\\' && s[i + 1] == 'u') {
                    i += 2;
                    const unsigned lo = hex4();
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                }
                utf8(out, cp);
                break;
            }
            default: fail("bad escape");
            }
        }
        return out;
    }
    Json value() {
        if (++depth > 64) fail("nesting too deep");
        ws();
        if (i >= s.size()) fail("unexpected end");
        Json out;
        const char c = s[i];
        if (c == '{') {
            i++;
            out = Json::object();
            if (!eat('}')) {
                do {
                    ws();
                    std::string k = str();
                    if (!eat(':')) fail("expected ':'");
                    out.set(std::move(k), value());
                } while (eat(','));
                if (!eat('}')) fail("expected '}'");
            }
        } else if (c == '[') {
            i++;
            out = Json::array();
            if (!eat(']')) {
                do out.push(value()); while (eat(','));
                if (!eat(']')) fail("expected ']'");
            }
        } else if (c == '"') {
            out = Json(str());
        } else if (s.compare(i, 4, "true") == 0) { i += 4; out = Json(true); }
        else if (s.compare(i, 5, "false") == 0) { i += 5; out = Json(false); }
        else if (s.compare(i, 4, "null") == 0) { i += 4; }
        else {
            const std::size_t start = i;
            if (s[i] == '-') i++;
            while (i < s.size() && ((s[i] >= '0' && s[i] <= '9') || s[i] == '.' || s[i] == 'e' || s[i] == 'E' || s[i] == '+' || s[i] == '-')) i++;
            if (i == start) fail("unexpected character");
            const std::string num(s.substr(start, i - start));
            char* end = nullptr;
            const double d = std::strtod(num.c_str(), &end);
            if (!end || *end != '\0' || !std::isfinite(d)) fail("bad number");
            out = Json(d);
        }
        depth--;
        return out;
    }
};

Json Json::parse(std::string_view text) {
    if (text.size() > 4 * 1024 * 1024) throw std::runtime_error("JSON: input too large");
    JsonParser p{text};
    Json v = p.value();
    p.ws();
    if (p.i != text.size()) p.fail("trailing characters");
    return v;
}

static void dumpString(std::string& out, const std::string& s) {
    out += '"';
    for (const char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char b[8];
                std::snprintf(b, sizeof b, "\\u%04x", c);
                out += b;
            } else out += c;
        }
    }
    out += '"';
}

static void dumpValue(std::string& out, const Json& v) {
    switch (v.type()) {
    case Json::Type::Null: out += "null"; break;
    case Json::Type::Bool: out += v.asBool() ? "true" : "false"; break;
    case Json::Type::Number: {
        const double d = v.asNumber();
        char b[32];
        if (d == std::floor(d) && std::fabs(d) < 1e15) std::snprintf(b, sizeof b, "%.0f", d);
        else std::snprintf(b, sizeof b, "%.17g", d);
        out += b;
        break;
    }
    case Json::Type::String: dumpString(out, v.asString()); break;
    case Json::Type::Array: {
        out += '[';
        bool first = true;
        for (const auto& item : v.items()) { if (!first) out += ','; first = false; dumpValue(out, item); }
        out += ']';
        break;
    }
    case Json::Type::Object: {
        out += '{';
        bool first = true;
        for (const auto& [k, item] : v.members()) {
            if (!first) out += ',';
            first = false;
            dumpString(out, k);
            out += ':';
            dumpValue(out, item);
        }
        out += '}';
        break;
    }
    }
}

std::string Json::dump() const {
    std::string out;
    dumpValue(out, *this);
    return out;
}

} // namespace sg
