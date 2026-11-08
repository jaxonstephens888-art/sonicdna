#include "sdna/Json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace sdna {

Json& Json::operator[](const std::string& k) {
    if (t != Type::Object) *this = object();
    for (auto& kv : o_) if (kv.first == k) return kv.second;
    o_.push_back({ k, Json() });
    return o_.back().second;
}
const Json& Json::get(const std::string& k) const {
    static const Json nil;
    if (t != Type::Object) return nil;
    for (auto& kv : o_) if (kv.first == k) return kv.second;
    return nil;
}
bool Json::has(const std::string& k) const {
    if (t != Type::Object) return false;
    for (auto& kv : o_) if (kv.first == k) return true;
    return false;
}

static void escapeTo(std::string& out, const std::string& s) {
    out += '"';
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); out += b; }
                else out += (char)c;
        }
    }
    out += '"';
}

void Json::dumpTo(std::string& out, int indent, int level) const {
    auto nl = [&](int lv) { if (indent > 0) { out += '\n'; out.append((size_t)(lv * indent), ' '); } };
    switch (t) {
        case Type::Null: out += "null"; break;
        case Type::Bool: out += b_ ? "true" : "false"; break;
        case Type::Number: {
            if (!std::isfinite(n_)) { out += "0"; break; }
            char b[32];
            if (std::abs(n_ - std::round(n_)) < 1e-9 && std::abs(n_) < 1e15) std::snprintf(b, sizeof b, "%lld", (long long)std::llround(n_));
            else std::snprintf(b, sizeof b, "%.6g", n_);
            out += b; break;
        }
        case Type::String: escapeTo(out, s_); break;
        case Type::Array:
            out += '[';
            for (size_t i = 0; i < a_.size(); ++i) { if (i) out += ','; nl(level + 1); a_[i].dumpTo(out, indent, level + 1); }
            if (!a_.empty()) nl(level);
            out += ']'; break;
        case Type::Object:
            out += '{';
            for (size_t i = 0; i < o_.size(); ++i) {
                if (i) out += ',';
                nl(level + 1); escapeTo(out, o_[i].first); out += indent > 0 ? ": " : ":";
                o_[i].second.dumpTo(out, indent, level + 1);
            }
            if (!o_.empty()) nl(level);
            out += '}'; break;
    }
}

std::string Json::dump(int indent) const { std::string s; dumpTo(s, indent, 0); return s; }

namespace {
struct Parser {
    const std::string& s; size_t i = 0; std::string err;
    explicit Parser(const std::string& str) : s(str) {}
    void ws() { while (i < s.size() && (s[i] == ' ' || s[i] == '\n' || s[i] == '\r' || s[i] == '\t')) ++i; }
    bool fail(const char* m) { if (err.empty()) err = std::string(m) + " at offset " + std::to_string(i); return false; }
    static void utf8(std::string& o, unsigned cp) {
        if (cp < 0x80) o += (char)cp;
        else if (cp < 0x800) { o += (char)(0xC0 | (cp >> 6)); o += (char)(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { o += (char)(0xE0 | (cp >> 12)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
        else { o += (char)(0xF0 | (cp >> 18)); o += (char)(0x80 | ((cp >> 12) & 0x3F)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
    }
    bool str(std::string& o) {
        if (s[i] != '"') return fail("expected string");
        ++i;
        while (i < s.size() && s[i] != '"') {
            char c = s[i++];
            if (c == '\\') {
                if (i >= s.size()) return fail("bad escape");
                char e = s[i++];
                switch (e) {
                    case 'n': o += '\n'; break; case 't': o += '\t'; break; case 'r': o += '\r'; break;
                    case 'b': o += '\b'; break; case 'f': o += '\f'; break;
                    case 'u': {
                        if (i + 4 > s.size()) return fail("bad unicode");
                        unsigned cp = (unsigned)std::strtoul(s.substr(i, 4).c_str(), nullptr, 16); i += 4;
                        if (cp >= 0xD800 && cp < 0xDC00 && i + 6 <= s.size() && s[i] == '\\' && s[i + 1] == 'u') {
                            unsigned lo = (unsigned)std::strtoul(s.substr(i + 2, 4).c_str(), nullptr, 16); i += 6;
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        }
                        utf8(o, cp); break;
                    }
                    default: o += e;
                }
            } else o += c;
        }
        if (i >= s.size()) return fail("unterminated string");
        ++i; return true;
    }
    bool value(Json& out, int depth) {
        if (depth > 200) return fail("too deep");
        ws();
        if (i >= s.size()) return fail("unexpected end");
        char c = s[i];
        if (c == '{') {
            ++i; out = Json::object(); ws();
            if (i < s.size() && s[i] == '}') { ++i; return true; }
            for (;;) {
                ws(); std::string k; if (!str(k)) return false;
                ws(); if (i >= s.size() || s[i] != ':') return fail("expected ':'"); ++i;
                Json v; if (!value(v, depth + 1)) return false;
                out[k] = std::move(v);
                ws(); if (i < s.size() && s[i] == ',') { ++i; continue; }
                if (i < s.size() && s[i] == '}') { ++i; return true; }
                return fail("expected ',' or '}'");
            }
        }
        if (c == '[') {
            ++i; out = Json::array(); ws();
            if (i < s.size() && s[i] == ']') { ++i; return true; }
            for (;;) {
                Json v; if (!value(v, depth + 1)) return false; out.push(std::move(v));
                ws(); if (i < s.size() && s[i] == ',') { ++i; continue; }
                if (i < s.size() && s[i] == ']') { ++i; return true; }
                return fail("expected ',' or ']'");
            }
        }
        if (c == '"') { std::string v; if (!str(v)) return false; out = Json(v); return true; }
        if (s.compare(i, 4, "true") == 0) { i += 4; out = Json(true); return true; }
        if (s.compare(i, 5, "false") == 0) { i += 5; out = Json(false); return true; }
        if (s.compare(i, 4, "null") == 0) { i += 4; out = Json(); return true; }
        const char* start = s.c_str() + i; char* end = nullptr;
        double d = std::strtod(start, &end);
        if (end == start) return fail("invalid value");
        i += (size_t)(end - start); out = Json(d); return true;
    }
};
}

Json Json::parse(const std::string& text, std::string* error) {
    Parser p(text);
    Json out;
    if (!p.value(out, 0)) { if (error) *error = p.err; return Json(); }
    if (error) error->clear();
    return out;
}

} // namespace sdna
