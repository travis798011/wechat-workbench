#include "JsonLite.h"

#include <cctype>
#include <sstream>

namespace {
void SkipWs(const std::string& s, size_t& i) {
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) {
        ++i;
    }
}

std::string ParseString(const std::string& s, size_t& i) {
    std::string out;
    if (i >= s.size() || s[i] != '"') return out;
    ++i;
    while (i < s.size()) {
        char c = s[i++];
        if (c == '"') break;
        if (c == '\\' && i < s.size()) {
            char e = s[i++];
            switch (e) {
            case '"': out.push_back('"'); break;
            case '\\': out.push_back('\\'); break;
            case '/': out.push_back('/'); break;
            case 'b': out.push_back('\b'); break;
            case 'f': out.push_back('\f'); break;
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 't': out.push_back('\t'); break;
            default: out.push_back(e); break;
            }
        } else {
            out.push_back(c);
        }
    }
    return out;
}
}

bool JsonObject::HasString(const std::string& key) const {
    return string_values.find(key) != string_values.end();
}

std::string JsonObject::GetString(const std::string& key, const std::string& def) const {
    auto it = string_values.find(key);
    return it == string_values.end() ? def : it->second;
}

long long JsonObject::GetInt(const std::string& key, long long def) const {
    auto n = number_values.find(key);
    if (n != number_values.end()) return n->second;
    auto s = string_values.find(key);
    if (s == string_values.end()) return def;
    try {
        return std::stoll(s->second);
    } catch (...) {
        return def;
    }
}

JsonObject JsonLite::ParseObject(const std::string& text) {
    JsonObject obj;
    size_t i = 0;
    SkipWs(text, i);
    if (i >= text.size() || text[i] != '{') return obj;
    ++i;

    while (i < text.size()) {
        SkipWs(text, i);
        if (i < text.size() && text[i] == '}') break;
        std::string key = ParseString(text, i);
        SkipWs(text, i);
        if (i >= text.size() || text[i] != ':') break;
        ++i;
        SkipWs(text, i);

        if (i < text.size() && text[i] == '"') {
            obj.string_values[key] = ParseString(text, i);
        } else {
            size_t begin = i;
            if (i < text.size() && (text[i] == '-' || text[i] == '+')) ++i;
            while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) ++i;
            if (begin != i) {
                try {
                    obj.number_values[key] = std::stoll(text.substr(begin, i - begin));
                } catch (...) {
                    obj.number_values[key] = 0;
                }
            }
        }

        SkipWs(text, i);
        if (i < text.size() && text[i] == ',') {
            ++i;
            continue;
        }
        if (i < text.size() && text[i] == '}') break;
    }
    return obj;
}

std::string JsonLite::Escape(const std::string& text) {
    std::ostringstream oss;
    for (char c : text) {
        switch (c) {
        case '"': oss << "\\\""; break;
        case '\\': oss << "\\\\"; break;
        case '\b': oss << "\\b"; break;
        case '\f': oss << "\\f"; break;
        case '\n': oss << "\\n"; break;
        case '\r': oss << "\\r"; break;
        case '\t': oss << "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                oss << "\\u00";
                const char* hex = "0123456789abcdef";
                oss << hex[(c >> 4) & 0xF] << hex[c & 0xF];
            } else {
                oss << c;
            }
            break;
        }
    }
    return oss.str();
}

std::string JsonLite::StringField(const std::string& key, const std::string& value) {
    return "\"" + Escape(key) + "\":\"" + Escape(value) + "\"";
}

std::string JsonLite::NumberField(const std::string& key, long long value) {
    return "\"" + Escape(key) + "\":" + std::to_string(value);
}

std::string JsonLite::Ok(const std::string& data_json) {
    return "{\"code\":1,\"msg\":\"success\",\"data\":" + data_json + "}";
}

std::string JsonLite::Error(int code, const std::string& msg) {
    return "{\"code\":" + std::to_string(code) + ",\"msg\":\"" + Escape(msg) + "\",\"data\":{}}";
}
