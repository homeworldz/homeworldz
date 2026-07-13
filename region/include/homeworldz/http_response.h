#pragma once

#include <string>
#include <string_view>

namespace homeworldz::http {

inline constexpr std::string_view request_id_header = "X-Request-ID";

struct Response {
    int status_code;
    std::string request_id;
    std::string method;
    std::string path;
    std::string content;
};

Response response_for(std::string_view request);

} // namespace homeworldz::http

