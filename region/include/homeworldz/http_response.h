#pragma once

#include <cstddef>
#include <optional>
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

std::optional<std::size_t> request_content_length(std::string_view request);
std::string request_header_value(std::string_view request, std::string_view name);
Response response_for(std::string_view request);
Response response_for(std::string_view request, std::string_view version);
Response response_for_content(std::string_view request, int status_code,
                              std::string_view content_type, std::string body);
// A 302 carrying only a Location, which is the whole protocol for the viewer's
// ServerReleaseNotes capability.
Response response_for_redirect(std::string_view request, std::string_view location);
// A 206 slice of full_body with Content-Range, for the ranged fetches viewer
// mesh loading performs (header first, then per-LOD extents).
Response response_for_range(std::string_view request, std::string_view content_type,
                            std::string_view full_body, std::size_t offset, std::size_t length);

// Add one header to an already-built response, after the status line. For the
// cases where a header is a property of the resource rather than of the reply
// shape - an ETag, Accept-Ranges - and so is not worth a parameter on every
// constructor. Does nothing to a malformed response rather than corrupting it.
void add_header(Response& response, std::string_view name, std::string_view value);

} // namespace homeworldz::http

