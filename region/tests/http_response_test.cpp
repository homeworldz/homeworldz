#include "homeworldz/api_models.h"
#include "homeworldz/http_response.h"

#include <iostream>
#include <string>

namespace {

bool contains(const std::string& value, const std::string& expected) {
    if (value.find(expected) != std::string::npos) return true;
    std::cerr << "response did not contain: " << expected << '\n' << value << '\n';
    return false;
}

} // namespace

int main() {
    bool passed = true;

    passed &= homeworldz::api::to_json(homeworldz::api::Status{"line\nbreak"}) ==
              R"({"status":"line\nbreak"})";
    passed &= homeworldz::api::to_json(homeworldz::api::Version{
                  "region", "1.2.3", std::string(homeworldz::api::api_version)}) ==
              R"({"service":"region","version":"1.2.3","apiVersion":"v1"})";
    passed &= homeworldz::api::to_json(homeworldz::api::Error{"bad_input", "quote: \""}) ==
              R"({"code":"bad_input","message":"quote: \""})";

    const auto ping = homeworldz::http::response_for(
        "GET /ping HTTP/1.1\r\nX-Request-ID: caller-request-123\r\n\r\n");
    passed &= ping.status_code == 200;
    passed &= ping.request_id == "caller-request-123";
    passed &= ping.method == "GET";
    passed &= ping.path == "/ping";
    passed &= contains(ping.content, "HTTP/1.1 200 OK");
    passed &= contains(ping.content, "X-Request-ID: caller-request-123");
    passed &= contains(ping.content, R"({"status":"ok"})");
    passed &= contains(ping.content, "Content-Length: 15");

    const auto ready = homeworldz::http::response_for("GET /ready HTTP/1.1\r\n\r\n");
    passed &= !ready.request_id.empty();
    passed &= contains(ready.content, R"({"status":"ready"})");

    const auto version = homeworldz::http::response_for("GET /version HTTP/1.1\r\n\r\n");
    passed &= contains(version.content, R"({"service":"region","version":"dev","apiVersion":"v1"})");

    const auto missing = homeworldz::http::response_for("GET /missing HTTP/1.1\r\n\r\n");
    passed &= missing.status_code == 404;
    passed &= contains(missing.content, "HTTP/1.1 404 Not Found");
    passed &= contains(missing.content, R"("code":"not_found")");

    const auto unsafe_id = homeworldz::http::response_for(
        "GET /ping HTTP/1.1\r\nX-Request-ID: unsafe value\r\n\r\n");
    passed &= unsafe_id.request_id != "unsafe value";
    passed &= unsafe_id.request_id.size() == 32;
    return passed ? 0 : 1;
}

