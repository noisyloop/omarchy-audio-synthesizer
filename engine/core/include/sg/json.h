#pragma once

// Minimal JSON value, parser and writer (no dependencies). Numbers are
// doubles parsed with strtod, which rounds exactly like JSON.parse.

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sg {

class Json {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Json() = default;
    Json(std::nullptr_t) {}
    Json(bool b) : type_(Type::Bool), bool_(b) {}
    Json(double d) : type_(Type::Number), num_(d) {}
    Json(int i) : type_(Type::Number), num_(i) {}
    Json(const char* s) : type_(Type::String), str_(s) {}
    Json(std::string s) : type_(Type::String), str_(std::move(s)) {}
    static Json array() { Json j; j.type_ = Type::Array; return j; }
    static Json object() { Json j; j.type_ = Type::Object; return j; }

    /** Throws std::runtime_error on malformed input or excessive depth/size. */
    static Json parse(std::string_view text);
    std::string dump() const;

    Type type() const noexcept { return type_; }
    bool isNull() const noexcept { return type_ == Type::Null; }
    bool isBool() const noexcept { return type_ == Type::Bool; }
    bool isNumber() const noexcept { return type_ == Type::Number; }
    bool isString() const noexcept { return type_ == Type::String; }
    bool isArray() const noexcept { return type_ == Type::Array; }
    bool isObject() const noexcept { return type_ == Type::Object; }

    bool asBool(bool def = false) const noexcept { return type_ == Type::Bool ? bool_ : def; }
    double asNumber(double def = 0) const noexcept { return type_ == Type::Number ? num_ : def; }
    const std::string& asString() const noexcept { static const std::string e; return type_ == Type::String ? str_ : e; }

    // arrays
    std::size_t size() const noexcept { return type_ == Type::Array ? arr_.size() : type_ == Type::Object ? obj_.size() : 0; }
    const Json& operator[](std::size_t i) const noexcept;
    void push(Json v) { type_ = Type::Array; arr_.push_back(std::move(v)); }
    const std::vector<Json>& items() const noexcept { return arr_; }

    // objects (insertion ordered)
    const Json& operator[](std::string_view key) const noexcept;
    bool has(std::string_view key) const noexcept;
    Json& set(std::string key, Json v);
    const std::vector<std::pair<std::string, Json>>& members() const noexcept { return obj_; }

private:
    Type type_ = Type::Null;
    bool bool_ = false;
    double num_ = 0;
    std::string str_;
    std::vector<Json> arr_;
    std::vector<std::pair<std::string, Json>> obj_;
    friend struct JsonParser;
};

} // namespace sg
