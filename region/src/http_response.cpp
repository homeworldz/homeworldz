#include "homeworldz/http_response.h"

namespace homeworldz::http {

std::string response_for(std::string_view request) {
    std::string_view status = "HTTP/1.1 404 Not Found\r\n";
    std::string_view body = R"({"code":"not_found","message":"route not found"})";
    if (request.starts_with("GET /ping ")) {
        status = "HTTP/1.1 200 OK\r\n";
        body = R"({"status":"ok"})";
    } else if (request.starts_with("GET /ready ")) {
        status = "HTTP/1.1 200 OK\r\n";
        body = R"({"status":"ready"})";
    } else if (request.starts_with("GET /version ")) {
        status = "HTTP/1.1 200 OK\r\n";
        body = R"({"service":"region","version":"dev","apiVersion":"v1"})";
    }
    return std::string(status) + "Content-Type: application/json\r\nConnection: close\r\nContent-Length: " +
           std::to_string(body.size()) + "\r\n\r\n" + std::string(body);
}

} // namespace homeworldz::http

