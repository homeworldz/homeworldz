#include "homeworldz/api_models.h"

namespace homeworldz::api {

std::string json_string(std::string_view value) {
    std::string result;
    result.reserve(value.size() + 2);
    result.push_back('"');
    for (const unsigned char character : value) {
        switch (character) {
        case '"': result += "\\\""; break;
        case '\\': result += "\\\\"; break;
        case '\b': result += "\\b"; break;
        case '\f': result += "\\f"; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default:
            if (character < 0x20) {
                constexpr char hex[] = "0123456789abcdef";
                result += "\\u00";
                result.push_back(hex[character >> 4]);
                result.push_back(hex[character & 0x0f]);
            } else {
                result.push_back(static_cast<char>(character));
            }
        }
    }
    result.push_back('"');
    return result;
}

std::string to_json(const Status& value) {
    return "{\"status\":" + json_string(value.status) + '}';
}

std::string to_json(const Version& value) {
    return "{\"service\":" + json_string(value.service) +
           ",\"version\":" + json_string(value.version) +
           ",\"apiVersion\":" + json_string(value.api_version) + '}';
}

std::string to_json(const Error& value) {
    return "{\"code\":" + json_string(value.code) +
           ",\"message\":" + json_string(value.message) + '}';
}

} // namespace homeworldz::api
