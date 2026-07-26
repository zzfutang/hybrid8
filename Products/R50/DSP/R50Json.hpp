//
//  R50Json.hpp
//  A small JSON reader, enough for the factory manifest and no more.
//
//  Hand-written rather than pulled in, and parsed in C++ rather than handed
//  over to NSJSONSerialization, for one reason: the manifest decides which
//  audio file becomes which instrument, and getting that wrong is silent — the
//  synth simply plays the wrong sample. That belongs where the offline suite
//  can test it, including the malformed cases.
//
//  Objects, arrays, strings, numbers, booleans and null. No unicode escapes
//  beyond the two that matter for a file path, and no exponent notation: the
//  manifest is written by our own tool and the format is a contract, so a
//  parser that accepts less than the whole of JSON is a feature. Anything it
//  cannot read is reported as a failure rather than guessed at.
//

#pragma once

#include <cstdlib>
#include <map>
#include <string>
#include <vector>

namespace r50 {

class JsonValue {
public:
    enum class Kind { Null, Bool, Number, String, Array, Object };

    Kind kind = Kind::Null;
    bool boolean = false;
    double number = 0.0;
    std::string text;
    std::vector<JsonValue> items;
    std::map<std::string, JsonValue> members;

    bool isObject() const { return kind == Kind::Object; }
    bool isArray() const { return kind == Kind::Array; }

    /// Missing members read as an empty value rather than throwing, so a caller
    /// can validate what it actually needs in one place.
    const JsonValue &operator[](const std::string &key) const {
        static const JsonValue empty;
        const auto found = members.find(key);
        return found == members.end() ? empty : found->second;
    }

    std::string stringOr(const std::string &fallback) const {
        return kind == Kind::String ? text : fallback;
    }

    int intOr(int fallback) const {
        return kind == Kind::Number ? static_cast<int>(number) : fallback;
    }
};

namespace detail {

inline void skipSpace(const std::string &in, size_t &at) {
    while (at < in.size() && (in[at] == ' ' || in[at] == '\t'
                           || in[at] == '\n' || in[at] == '\r')) {
        ++at;
    }
}

bool parseValue(const std::string &in, size_t &at, JsonValue &out);

inline bool parseString(const std::string &in, size_t &at, std::string &out) {
    if (at >= in.size() || in[at] != '"') return false;
    ++at;
    out.clear();
    while (at < in.size()) {
        const char c = in[at++];
        if (c == '"') return true;
        if (c != '\\') { out += c; continue; }
        if (at >= in.size()) return false;
        const char escaped = in[at++];
        switch (escaped) {
            case '"':  out += '"';  break;
            case '\\': out += '\\'; break;
            case '/':  out += '/';  break;
            case 'n':  out += '\n'; break;
            case 't':  out += '\t'; break;
            case 'r':  out += '\r'; break;
            case 'b':  out += '\b'; break;
            case 'f':  out += '\f'; break;
            default:   return false;   // including \u: not needed, not guessed
        }
    }
    return false;
}

inline bool parseValue(const std::string &in, size_t &at, JsonValue &out) {
    skipSpace(in, at);
    if (at >= in.size()) return false;

    const char c = in[at];
    if (c == '"') {
        out.kind = JsonValue::Kind::String;
        return parseString(in, at, out.text);
    }
    if (c == '{') {
        ++at;
        out.kind = JsonValue::Kind::Object;
        skipSpace(in, at);
        if (at < in.size() && in[at] == '}') { ++at; return true; }
        while (true) {
            skipSpace(in, at);
            std::string key;
            if (!parseString(in, at, key)) return false;
            skipSpace(in, at);
            if (at >= in.size() || in[at] != ':') return false;
            ++at;
            JsonValue value;
            if (!parseValue(in, at, value)) return false;
            out.members[key] = value;
            skipSpace(in, at);
            if (at >= in.size()) return false;
            if (in[at] == ',') { ++at; continue; }
            if (in[at] == '}') { ++at; return true; }
            return false;
        }
    }
    if (c == '[') {
        ++at;
        out.kind = JsonValue::Kind::Array;
        skipSpace(in, at);
        if (at < in.size() && in[at] == ']') { ++at; return true; }
        while (true) {
            JsonValue value;
            if (!parseValue(in, at, value)) return false;
            out.items.push_back(value);
            skipSpace(in, at);
            if (at >= in.size()) return false;
            if (in[at] == ',') { ++at; continue; }
            if (in[at] == ']') { ++at; return true; }
            return false;
        }
    }
    if (in.compare(at, 4, "true") == 0) {
        at += 4; out.kind = JsonValue::Kind::Bool; out.boolean = true; return true;
    }
    if (in.compare(at, 5, "false") == 0) {
        at += 5; out.kind = JsonValue::Kind::Bool; out.boolean = false; return true;
    }
    if (in.compare(at, 4, "null") == 0) {
        at += 4; out.kind = JsonValue::Kind::Null; return true;
    }
    if (c == '-' || (c >= '0' && c <= '9')) {
        const size_t start = at;
        if (in[at] == '-') ++at;
        while (at < in.size() && ((in[at] >= '0' && in[at] <= '9') || in[at] == '.')) {
            ++at;
        }
        if (at == start) return false;
        out.kind = JsonValue::Kind::Number;
        out.number = std::strtod(in.substr(start, at - start).c_str(), nullptr);
        return true;
    }
    return false;
}

} // namespace detail

/// Returns false on anything malformed, including trailing rubbish — a
/// manifest that half-parses would map some samples and silently drop others.
inline bool parseJson(const std::string &in, JsonValue &out) {
    size_t at = 0;
    if (!detail::parseValue(in, at, out)) return false;
    detail::skipSpace(in, at);
    return at == in.size();
}

} // namespace r50
