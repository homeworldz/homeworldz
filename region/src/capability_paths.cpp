#include "homeworldz/capability_paths.h"

#include "homeworldz/viewer_protocol.h"

namespace homeworldz::caps {

namespace {

// Both capabilities put the session before the query and accept a trailing
// slash, so the split is shared rather than written twice.
struct SessionAndQuery {
    std::string_view session;
    std::string_view query;
};

std::optional<SessionAndQuery> split(std::string_view path, std::string_view prefix) {
    if (!path.starts_with(prefix)) return std::nullopt;
    const auto question = path.find('?', prefix.size());
    if (question == std::string_view::npos) return std::nullopt;
    auto session = path.substr(prefix.size(), question - prefix.size());
    if (!session.empty() && session.back() == '/') session.remove_suffix(1);
    if (session.empty()) return std::nullopt;
    const auto query = path.substr(question + 1);
    // One asset per request: a second parameter would make "which id" ambiguous
    // and there is no caller that needs it.
    if (query.find('&') != std::string_view::npos) return std::nullopt;
    return SessionAndQuery{session, query};
}

} // namespace

std::optional<TextureRequest> texture_request(std::string_view path) {
    constexpr std::string_view prefix = "/caps/texture/";
    constexpr std::string_view key = "texture_id=";
    const auto parts = split(path, prefix);
    if (!parts || !parts->query.starts_with(key)) return std::nullopt;
    const auto texture = parts->query.substr(key.size());
    if (texture.empty()) return std::nullopt;
    return TextureRequest{std::string(parts->session), std::string(texture)};
}

std::optional<ViewerAssetRequest> viewer_asset_request(std::string_view path) {
    constexpr std::string_view prefix = "/caps/assets/";
    const auto parts = split(path, prefix);
    if (!parts) return std::nullopt;
    // The type prefix varies (texture_id, bodypart_id, clothing_id, mesh_id,
    // ...); "_id=" is common to all and identifies the requested UUID.
    const auto marker = parts->query.find("_id=");
    if (marker == std::string_view::npos || marker == 0) return std::nullopt;
    const auto asset = parts->query.substr(marker + 4);
    if (asset.empty()) return std::nullopt;
    return ViewerAssetRequest{std::string(parts->session), std::string(asset),
                              parts->query.starts_with("mesh_id="),
                              parts->query.starts_with("texture_id=")};
}

std::string capability_session(std::string_view path, std::string_view prefix) {
    if (!path.starts_with(prefix)) return {};
    auto session = path.substr(prefix.size());
    if (const auto separator = session.find('/'); separator != std::string_view::npos) {
        const auto visit = session.substr(separator + 1);
        if (visit.empty() || visit.find('/') != std::string_view::npos ||
            !viewer::parse_uuid(visit)) return {};
        session = session.substr(0, separator);
    }
    if (session.empty()) return {};
    return std::string(session);
}

std::string capability_visit(std::string_view path, std::string_view prefix) {
    if (!path.starts_with(prefix)) return {};
    const auto remainder = path.substr(prefix.size());
    const auto separator = remainder.find('/');
    if (separator == std::string_view::npos) return {};
    const auto visit = remainder.substr(separator + 1);
    if (visit.empty() || visit.find('/') != std::string_view::npos ||
        !viewer::parse_uuid(visit)) return {};
    return std::string(visit);
}

} // namespace homeworldz::caps
