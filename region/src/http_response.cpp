#include "homeworldz/http_response.h"

#include "homeworldz/api_models.h"

namespace homeworldz::http {

std::string response_for(std::string_view request) {
    std::string_view status = "HTTP/1.1 404 Not Found\r\n";
    std::string body = api::to_json(api::Error{"not_found", "route not found"});
    if (request.starts_with("GET /ping ")) {
        status = "HTTP/1.1 200 OK\r\n";
        body = api::to_json(api::Status{"ok"});
    } else if (request.starts_with("GET /ready ")) {
        status = "HTTP/1.1 200 OK\r\n";
        body = api::to_json(api::Status{"ready"});
    } else if (request.starts_with("GET /version ")) {
        status = "HTTP/1.1 200 OK\r\n";
        body = api::to_json(api::Version{"region", "dev", std::string(api::api_version)});
    }
    return std::string(status) + "Content-Type: application/json\r\nConnection: close\r\nContent-Length: " +
           std::to_string(body.size()) + "\r\n\r\n" + body;
}

} // namespace homeworldz::http

