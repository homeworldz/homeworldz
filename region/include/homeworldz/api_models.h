#pragma once

#include <string>
#include <string_view>

namespace homeworldz::api {

inline constexpr std::string_view api_version = "v1";

struct Status {
    std::string status;
};

struct Version {
    std::string service;
    std::string version;
    std::string api_version;
};

struct Error {
    std::string code;
    std::string message;
};

std::string json_string(std::string_view value);
std::string to_json(const Status& value);
std::string to_json(const Version& value);
std::string to_json(const Error& value);

} // namespace homeworldz::api
