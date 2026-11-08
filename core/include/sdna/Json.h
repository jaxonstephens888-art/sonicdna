// Minimal JSON value / parser / writer (UTF-8 passthrough). Enough for kit
// files, settings and parsing AI analysis responses.
#pragma once

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace sdna {

class Json {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };
    Json() = default;
    Json(std::nullptr_t) {}
    Json(bool b) : t(Type::Bool), b_(b) {}
    Json(int v) : t(Type::Number), n_(v) {}
    Json(long long v) : t(Type::Number), n_((double)v) {}
    Json(unsigned long long v) : t(Type::Number), n_((double)v) {}
    Json(double v) : t(Type::Number), n_(v) {}
    Json(float v) : t(Type::Number), n_(v) {}
    Json(const char* s) : t(Type::String), s_(s) {}
    Json(std::string s) : t(Type::String), s_(std::move(s)) {}

    static Json array() { Json j; j.t = Type::Array; return j; }
    static Json object() { Json j; j.t = Type::Object; return j; }

    Type type() const { return t; }
    bool isNull() const { return t == Type::Null; }
    bool isObject() const { return t == Type::Object; }
    bool isArray() const { return t == Type::Array; }
    bool isString() const { return t == Type::String; }
    bool isNumber() const { return t == Type::Number; }

    double num(double def = 0) const { return t == Type::Number ? n_ : (t == Type::Bool ? (b_ ? 1 : 0) : def); }
    bool boolean(bool def = false) const { return t == Type::Bool ? b_ : (t == Type::Number ? n_ != 0 : def); }
    const std::string& str() const { static const std::string e; return t == Type::String ? s_ : e; }
    std::string str(const std::string& def) const { return t == Type::String ? s_ : def; }

    // Array
    void push(Json v) { if (t != Type::Array) { *this = array(); } a_.push_back(std::move(v)); }
    size_t size() const { return t == Type::Array ? a_.size() : (t == Type::Object ? o_.size() : 0); }
    const Json& operator[](size_t i) const { static const Json nil; return (t == Type::Array && i < a_.size()) ? a_[i] : nil; }
    const std::vector<Json>& items() const { return a_; }

    // Object (insertion ordered)
    Json& operator[](const std::string& k);
    const Json& get(const std::string& k) const;
    bool has(const std::string& k) const;
    const std::vector<std::pair<std::string, Json>>& members() const { return o_; }

    std::string dump(int indent = 2) const;
    static Json parse(const std::string& text, std::string* error = nullptr);

private:
    void dumpTo(std::string& out, int indent, int level) const;
    Type t = Type::Null;
    bool b_ = false;
    double n_ = 0;
    std::string s_;
    std::vector<Json> a_;
    std::vector<std::pair<std::string, Json>> o_;
};

} // namespace sdna
