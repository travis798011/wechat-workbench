#pragma once

#include <map>
#include <string>

class JsonObject {
public:
    std::map<std::string, std::string> string_values;
    std::map<std::string, long long> number_values;

    bool HasString(const std::string& key) const;
    std::string GetString(const std::string& key, const std::string& def = "") const;
    long long GetInt(const std::string& key, long long def = 0) const;
};

namespace JsonLite {
    JsonObject ParseObject(const std::string& text);
    std::string Escape(const std::string& text);
    std::string StringField(const std::string& key, const std::string& value);
    std::string NumberField(const std::string& key, long long value);
    std::string Ok(const std::string& data_json = "{}");
    std::string Error(int code, const std::string& msg);
}
