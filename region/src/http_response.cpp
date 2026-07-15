#include "homeworldz/http_response.h"

#include "homeworldz/api_models.h"

#include <atomic>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <utility>

namespace homeworldz::http {
namespace {

bool ascii_equal_insensitive(std::string_view left, std::string_view right) {
    if (left.size() != right.size()) return false;
    for (std::size_t index = 0; index < left.size(); ++index) {
        char a = left[index];
        char b = right[index];
        if (a >= 'A' && a <= 'Z') a = static_cast<char>(a + ('a' - 'A'));
        if (b >= 'A' && b <= 'Z') b = static_cast<char>(b + ('a' - 'A'));
        if (a != b) return false;
    }
    return true;
}

bool valid_request_id(std::string_view value) {
    if (value.empty() || value.size() > 128) return false;
    for (const char character : value) {
        if ((character >= 'a' && character <= 'z') ||
            (character >= 'A' && character <= 'Z') ||
            (character >= '0' && character <= '9') ||
            character == '-' || character == '_' || character == '.') {
            continue;
        }
        return false;
    }
    return true;
}

std::string parse_request_header(std::string_view request, std::string_view wanted_name) {
    auto position = request.find("\r\n");
    if (position == std::string_view::npos) return {};
    position += 2;
    while (position < request.size()) {
        const auto end = request.find("\r\n", position);
        if (end == std::string_view::npos || end == position) break;
        const auto line = request.substr(position, end - position);
        const auto colon = line.find(':');
        if (colon != std::string_view::npos &&
            ascii_equal_insensitive(line.substr(0, colon), wanted_name)) {
            auto value = line.substr(colon + 1);
            while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
                value.remove_prefix(1);
            }
            while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) {
                value.remove_suffix(1);
            }
            return std::string(value);
        }
        position = end + 2;
    }
    return {};
}

std::string new_request_id() {
    static std::atomic<std::uint64_t> sequence{0};
    const auto timestamp = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    const auto next = sequence.fetch_add(1, std::memory_order_relaxed) + 1;
    constexpr char hex[] = "0123456789abcdef";
    std::string result(32, '0');
    for (int index = 0; index < 16; ++index) {
        const auto value = index < 8 ? timestamp : next;
        const auto offset = index < 8 ? index : index - 8;
        result[index * 2] = hex[(value >> ((15 - offset * 2) * 4)) & 0x0f];
        result[index * 2 + 1] = hex[(value >> ((14 - offset * 2) * 4)) & 0x0f];
    }
    return result;
}

void parse_request_line(std::string_view request, std::string& method, std::string& path) {
    const auto first_space = request.find(' ');
    if (first_space == std::string_view::npos) return;
    method = request.substr(0, first_space);
    const auto second_space = request.find(' ', first_space + 1);
    if (second_space == std::string_view::npos) return;
    path = request.substr(first_space + 1, second_space - first_space - 1);
}

} // namespace

std::optional<std::size_t> request_content_length(std::string_view request) {
    const auto value = parse_request_header(request, "Content-Length");
    if (value.empty()) return 0;
    std::size_t length{};
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), length);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()) return std::nullopt;
    return length;
}

std::string request_header_value(std::string_view request, std::string_view name) {
    return parse_request_header(request, name);
}

Response response_for_content(std::string_view request, int status_code,
                              std::string_view content_type, std::string body) {
    std::string method;
    std::string path;
    parse_request_line(request, method, path);
    std::string_view status = "HTTP/1.1 500 Internal Server Error\r\n";
    if (status_code == 200) status = "HTTP/1.1 200 OK\r\n";
    else if (status_code == 400) status = "HTTP/1.1 400 Bad Request\r\n";
    else if (status_code == 401) status = "HTTP/1.1 401 Unauthorized\r\n";
    else if (status_code == 404) status = "HTTP/1.1 404 Not Found\r\n";
    else if (status_code == 405) status = "HTTP/1.1 405 Method Not Allowed\r\n";
    auto request_id = parse_request_header(request, request_id_header);
    if (!valid_request_id(request_id)) request_id = new_request_id();
    auto content = std::string(status) + "Content-Type: " + std::string(content_type) +
                   "\r\nConnection: close\r\n" + std::string(request_id_header) + ": " + request_id +
                   "\r\nContent-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
    return {status_code, std::move(request_id), std::move(method), std::move(path), std::move(content)};
}

Response response_for(std::string_view request) {
    return response_for(request, HOMEWORLDZ_VERSION);
}

Response response_for(std::string_view request, std::string_view version) {
    std::string method;
    std::string path;
    parse_request_line(request, method, path);
    int status_code = 404;
    std::string_view status = "HTTP/1.1 404 Not Found\r\n";
    std::string body = api::to_json(api::Error{"not_found", "route not found"});
    std::string_view additional_headers;
    if (method == "GET" && path == "/ping") {
        status_code = 200;
        status = "HTTP/1.1 200 OK\r\n";
        body = api::to_json(api::Status{"ok"});
    } else if (method == "GET" && path == "/ready") {
        status_code = 200;
        status = "HTTP/1.1 200 OK\r\n";
        body = api::to_json(api::Status{"ready"});
    } else if (method == "GET" && path == "/version") {
        status_code = 200;
        status = "HTTP/1.1 200 OK\r\n";
        body = api::to_json(api::Version{"region", std::string(version), std::string(api::api_version)});
    } else if (path == "/ping" || path == "/ready" || path == "/version") {
        status_code = 405;
        status = "HTTP/1.1 405 Method Not Allowed\r\n";
        additional_headers = "Allow: GET\r\n";
        body = api::to_json(api::Error{"method_not_allowed", "only GET is supported"});
    }
    auto request_id = parse_request_header(request, request_id_header);
    if (!valid_request_id(request_id)) request_id = new_request_id();
    auto content = std::string(status) + std::string(additional_headers) +
                   "Content-Type: application/json\r\nConnection: close\r\n" +
                   std::string(request_id_header) + ": " + request_id + "\r\nContent-Length: " +
                   std::to_string(body.size()) + "\r\n\r\n" + body;
    return {status_code, std::move(request_id), std::move(method), std::move(path), std::move(content)};
}

} // namespace homeworldz::http

