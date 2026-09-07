#pragma once
// Minimal JSON reader/writer for RCAI (crash dumps, runtime data tables).
// Dependency-free, C++17. Not a full JSON: numbers/strings/bools/null/arrays/objects.
#include <cctype>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace rcai::json {

class Value;
using Object = std::vector<std::pair<std::string, Value>>; // insertion-ordered
using Array = std::vector<Value>;

class Value {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Value() : type_(Type::Null) {}
    Value(bool b) : type_(Type::Bool), bool_(b) {}
    Value(double n) : type_(Type::Number), num_(n) {}
    Value(float n) : type_(Type::Number), num_(n) {}
    Value(int n) : type_(Type::Number), num_(n) {}
    Value(const char* s) : type_(Type::String), str_(s) {}
    Value(std::string s) : type_(Type::String), str_(std::move(s)) {}
    Value(Array a) : type_(Type::Array), arr_(std::move(a)) {}
    Value(Object o) : type_(Type::Object), obj_(std::move(o)) {}

    Type type() const { return type_; }
    bool isNull() const { return type_ == Type::Null; }
    bool isObject() const { return type_ == Type::Object; }
    bool isArray() const { return type_ == Type::Array; }
    bool isString() const { return type_ == Type::String; }
    bool isNumber() const { return type_ == Type::Number; }
    bool isBool() const { return type_ == Type::Bool; }

    bool asBool(bool def = false) const { return isBool() ? bool_ : def; }
    double asNumber(double def = 0.0) const { return isNumber() ? num_ : def; }
    float asFloat(float def = 0.f) const { return static_cast<float>(asNumber(def)); }
    int asInt(int def = 0) const { return isNumber() ? static_cast<int>(asNumber()) : def; }
    const std::string& asString() const { static const std::string e; return isString() ? str_ : e; }
    const Array& asArray() const { static const Array e; return isArray() ? arr_ : e; }
    const Object& asObject() const { static const Object e; return isObject() ? obj_ : e; }

    Value* find(const std::string& key) {
        if (!isObject()) return nullptr;
        for (auto& kv : obj_) if (kv.first == key) return &kv.second;
        return nullptr;
    }
    const Value* find(const std::string& key) const {
        if (!isObject()) return nullptr;
        for (auto& kv : obj_) if (kv.first == key) return &kv.second;
        return nullptr;
    }

    Value& set(const std::string& key, Value v) {
        if (type_ == Type::Null) type_ = Type::Object;
        if (!isObject()) throw std::runtime_error("json: set() on non-object");
        for (auto& kv : obj_)
            if (kv.first == key) { kv.second = std::move(v); return kv.second; }
        obj_.emplace_back(key, std::move(v));
        return obj_.back().second;
    }

    void push(Value v) {
        if (type_ == Type::Null) type_ = Type::Array;
        if (!isArray()) throw std::runtime_error("json: push() on non-array");
        arr_.push_back(std::move(v));
    }

    std::string dump(int indent = 2, int depth = 0) const {
        std::string pad(static_cast<size_t>(indent * depth), ' ');
        std::string padIn(static_cast<size_t>(indent * (depth + 1)), ' ');
        const char* nl = indent > 0 ? "\n" : "";
        switch (type_) {
        case Type::Null: return "null";
        case Type::Bool: return bool_ ? "true" : "false";
        case Type::Number: {
            std::ostringstream os;
            os.precision(9);
            os << num_;
            return os.str();
        }
        case Type::String: return escape(str_);
        case Type::Array: {
            if (arr_.empty()) return "[]";
            std::string s = "[" + std::string(nl);
            for (size_t i = 0; i < arr_.size(); ++i) {
                s += padIn + arr_[i].dump(indent, depth + 1);
                if (i + 1 < arr_.size()) s += ",";
                s += std::string(nl);
            }
            return s + pad + "]";
        }
        case Type::Object: {
            if (obj_.empty()) return "{}";
            std::string s = "{" + std::string(nl);
            for (size_t i = 0; i < obj_.size(); ++i) {
                s += padIn + escape(obj_[i].first) + ":" + (indent > 0 ? " " : "") +
                     obj_[i].second.dump(indent, depth + 1);
                if (i + 1 < obj_.size()) s += ",";
                s += std::string(nl);
            }
            return s + pad + "}";
        }
        }
        return "null";
    }

    static std::string escape(const std::string& s) {
        std::string out = "\"";
        for (char c : s) {
            switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\t': out += "\\t"; break;
            case '\r': out += "\\r"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += c;
                }
            }
        }
        out += "\"";
        return out;
    }

    static Value parse(const std::string& text) {
        size_t i = 0;
        Value v = parseValue(text, i);
        skipWs(text, i);
        if (i != text.size()) throw std::runtime_error("json: trailing data");
        return v;
    }

private:
    Type type_;
    bool bool_ = false;
    double num_ = 0;
    std::string str_;
    Array arr_;
    Object obj_;

    static void skipWs(const std::string& t, size_t& i) {
        while (i < t.size() && std::isspace(static_cast<unsigned char>(t[i]))) ++i;
    }

    static Value parseValue(const std::string& t, size_t& i) {
        skipWs(t, i);
        if (i >= t.size()) throw std::runtime_error("json: unexpected end");
        const char c = t[i];
        if (c == '{') return parseObject(t, i);
        if (c == '[') return parseArray(t, i);
        if (c == '"') return Value(parseString(t, i));
        if (c == 't') { expect(t, i, "true"); return Value(true); }
        if (c == 'f') { expect(t, i, "false"); return Value(false); }
        if (c == 'n') { expect(t, i, "null"); return Value(); }
        return parseNumber(t, i);
    }

    static void expect(const std::string& t, size_t& i, const char* lit) {
        for (const char* p = lit; *p; ++p, ++i)
            if (i >= t.size() || t[i] != *p) throw std::runtime_error("json: bad literal");
    }

    static Value parseObject(const std::string& t, size_t& i) {
        ++i; // {
        Object obj;
        skipWs(t, i);
        if (i < t.size() && t[i] == '}') { ++i; return Value(std::move(obj)); }
        while (true) {
            skipWs(t, i);
            std::string key = parseString(t, i);
            skipWs(t, i);
            if (i >= t.size() || t[i] != ':') throw std::runtime_error("json: expected ':'");
            ++i;
            obj.emplace_back(std::move(key), parseValue(t, i));
            skipWs(t, i);
            if (i < t.size() && t[i] == ',') { ++i; continue; }
            if (i < t.size() && t[i] == '}') { ++i; break; }
            throw std::runtime_error("json: expected ',' or '}'");
        }
        return Value(std::move(obj));
    }

    static Value parseArray(const std::string& t, size_t& i) {
        ++i; // [
        Array arr;
        skipWs(t, i);
        if (i < t.size() && t[i] == ']') { ++i; return Value(std::move(arr)); }
        while (true) {
            arr.push_back(parseValue(t, i));
            skipWs(t, i);
            if (i < t.size() && t[i] == ',') { ++i; continue; }
            if (i < t.size() && t[i] == ']') { ++i; break; }
            throw std::runtime_error("json: expected ',' or ']'");
        }
        return Value(std::move(arr));
    }

    static std::string parseString(const std::string& t, size_t& i) {
        if (i >= t.size() || t[i] != '"') throw std::runtime_error("json: expected string");
        ++i;
        std::string out;
        while (i < t.size() && t[i] != '"') {
            char c = t[i++];
            if (c == '\\' && i < t.size()) {
                const char e = t[i++];
                switch (e) {
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'r': out += '\r'; break;
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'u': {
                    if (i + 4 > t.size()) throw std::runtime_error("json: bad \\u");
                    int cp = std::stoi(t.substr(i, 4), nullptr, 16);
                    i += 4;
                    if (cp < 128) out += static_cast<char>(cp);
                    else out += "\uFFFD";
                    break;
                }
                default: out += e;
                }
            } else {
                out += c;
            }
        }
        if (i >= t.size()) throw std::runtime_error("json: unterminated string");
        ++i;
        return out;
    }

    static Value parseNumber(const std::string& t, size_t& i) {
        const size_t start = i;
        while (i < t.size() && (std::isdigit(static_cast<unsigned char>(t[i])) || t[i] == '-' ||
                                t[i] == '+' || t[i] == '.' || t[i] == 'e' || t[i] == 'E'))
            ++i;
        if (i == start) throw std::runtime_error("json: bad number");
        return Value(std::stod(t.substr(start, i - start)));
    }
};

} // namespace rcai::json
