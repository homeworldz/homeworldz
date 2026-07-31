// Parsing the capability URLs a viewer fetches from.
//
// These lived in main.cpp's anonymous namespace, where no test could reach
// them, and that is not an incidental detail: the rule that a viewer is served
// the derived JPEG2000 rather than the canonical PNG was implemented against
// `texture_request` alone, while Firestorm fetches through the ViewerAsset
// capability that `viewer_asset_request` parses. Every extracted texture
// therefore reached viewers as a PNG their decoder refuses, and every
// server-side check passed while it happened (found live 2026-07-31).
//
// Two parsers that must agree about what a texture is, with no test able to
// ask either of them, is the defect. They are one family and they live
// together now.
#ifndef HOMEWORLDZ_CAPABILITY_PATHS_H
#define HOMEWORLDZ_CAPABILITY_PATHS_H

#include <optional>
#include <string>
#include <string_view>

namespace homeworldz::caps {

// "/caps/texture/<session>/?texture_id=<uuid>" — the legacy GetTexture
// capability. Viewers differ on whether they append the slash before the query,
// so both spellings parse.
struct TextureRequest {
    std::string session;
    std::string texture;
};
std::optional<TextureRequest> texture_request(std::string_view path);

// "/caps/assets/<session>/?<type>_id=<uuid>" — the ViewerAsset capability,
// which carries every asset kind behind a common "_id=" marker. The flags say
// what the request is *for*, because what a viewer is handed depends on it: a
// mesh gets the sl-mesh rendition, a texture the j2c-texture rendition, and
// anything else its canonical bytes.
struct ViewerAssetRequest {
    std::string session;
    std::string asset;
    bool mesh{};
    bool texture{};
};
std::optional<ViewerAssetRequest> viewer_asset_request(std::string_view path);

// The session id in "<prefix><session>" or "<prefix><session>/<visit uuid>".
// Empty when the path does not match or the visit is malformed.
std::string capability_session(std::string_view path, std::string_view prefix);

// The visit id in "<prefix><session>/<visit uuid>". Empty when absent.
std::string capability_visit(std::string_view path, std::string_view prefix);

// Whether a ViewerAsset request and a GetTexture request name the same thing:
// a texture fetch, whichever capability carried it. The region resolves both
// through one branch so the two cannot drift apart again.
inline std::optional<TextureRequest> texture_fetch(
    std::string_view path, const std::optional<ViewerAssetRequest>& viewer_asset) {
    if (auto direct = texture_request(path)) return direct;
    if (viewer_asset && viewer_asset->texture)
        return TextureRequest{viewer_asset->session, viewer_asset->asset};
    return std::nullopt;
}

} // namespace homeworldz::caps

#endif
