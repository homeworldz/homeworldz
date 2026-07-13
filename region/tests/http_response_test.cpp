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

    const auto ping = homeworldz::http::response_for("GET /ping HTTP/1.1\r\n\r\n");
    passed &= contains(ping, "HTTP/1.1 200 OK");
    passed &= contains(ping, R"({"status":"ok"})");
    passed &= contains(ping, "Content-Length: 15");

    const auto ready = homeworldz::http::response_for("GET /ready HTTP/1.1\r\n\r\n");
    passed &= contains(ready, R"({"status":"ready"})");

    const auto version = homeworldz::http::response_for("GET /version HTTP/1.1\r\n\r\n");
    passed &= contains(version, R"({"service":"region","version":"dev","apiVersion":"v1"})");

    const auto missing = homeworldz::http::response_for("GET /missing HTTP/1.1\r\n\r\n");
    passed &= contains(missing, "HTTP/1.1 404 Not Found");
    passed &= contains(missing, R"("code":"not_found")");
    return passed ? 0 : 1;
}

