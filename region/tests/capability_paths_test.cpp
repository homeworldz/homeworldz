// The capability path parsers, and above all the agreement between them.
//
// The bug this file exists for: a texture served through ViewerAsset reached
// Firestorm as the canonical PNG rather than the derived JPEG2000, because the
// rule was written against the GetTexture parser only. Nothing could catch it
// because neither parser was reachable from a test. The last section here is
// the one that matters — it asserts that both capabilities agree a texture is
// a texture.
#include "homeworldz/capability_paths.h"

#include <iostream>
#include <string>

using homeworldz::caps::capability_session;
using homeworldz::caps::capability_visit;
using homeworldz::caps::texture_fetch;
using homeworldz::caps::texture_request;
using homeworldz::caps::viewer_asset_request;

namespace {

int failures = 0;

void check(bool condition, const std::string& what) {
    if (!condition) {
        std::cerr << "FAIL: " << what << '\n';
        ++failures;
    }
}

} // namespace

int main() {
    constexpr const char* session = "60e9ead5-8827-4c0b-8f3d-2da9eaff2b6a";
    constexpr const char* asset = "0d61d564-00ae-4388-a86a-cc73b480e211";

    // --- GetTexture, both spellings viewers use.
    {
        const auto with_slash = texture_request(
            std::string("/caps/texture/") + session + "/?texture_id=" + asset);
        const auto without = texture_request(
            std::string("/caps/texture/") + session + "?texture_id=" + asset);
        check(with_slash && with_slash->session == session && with_slash->texture == asset,
              "GetTexture with a trailing slash before the query");
        check(without && without->session == session && without->texture == asset,
              "GetTexture without a trailing slash");
        check(!texture_request(std::string("/caps/texture/") + session),
              "GetTexture with no query is not a request");
        check(!texture_request(std::string("/caps/texture/?texture_id=") + asset),
              "GetTexture with no session is not a request");
        check(!texture_request(std::string("/caps/texture/") + session + "/?texture_id="),
              "GetTexture with an empty id is not a request");
        check(!texture_request(std::string("/caps/assets/") + session + "/?texture_id=" + asset),
              "GetTexture does not claim the ViewerAsset path");
    }

    // --- ViewerAsset, and the flags that decide what a viewer is handed.
    {
        const auto texture = viewer_asset_request(
            std::string("/caps/assets/") + session + "/?texture_id=" + asset);
        check(texture && texture->asset == asset && texture->texture && !texture->mesh,
              "ViewerAsset texture_id sets the texture flag");

        const auto mesh = viewer_asset_request(
            std::string("/caps/assets/") + session + "/?mesh_id=" + asset);
        check(mesh && mesh->mesh && !mesh->texture, "ViewerAsset mesh_id sets the mesh flag");

        // A wearable is neither: it is served its canonical bytes, and must not
        // be diverted through an image or mesh rendition.
        for (const auto* kind : {"bodypart_id=", "clothing_id=", "sound_id=", "animation_id="}) {
            const auto other = viewer_asset_request(
                std::string("/caps/assets/") + session + "/?" + kind + asset);
            check(other && other->asset == asset && !other->mesh && !other->texture,
                  std::string("ViewerAsset ") + kind + " sets no rendition flag");
        }

        check(!viewer_asset_request(std::string("/caps/assets/") + session + "/?_id=" + asset),
              "ViewerAsset with no type before _id is not a request");
        check(!viewer_asset_request(std::string("/caps/assets/") + session + "/?texture_id="),
              "ViewerAsset with an empty id is not a request");
        check(!viewer_asset_request(std::string("/caps/assets/") + session +
                                    "/?texture_id=" + asset + "&extra=1"),
              "ViewerAsset refuses a second parameter rather than guessing");
        check(!viewer_asset_request("/caps/assets/?texture_id="),
              "ViewerAsset with neither session nor id is not a request");
    }

    // --- The agreement. This is the regression test for 2026-07-31: a texture
    // is a texture whichever capability asked, and the region must resolve both
    // through the same branch or one of them serves the wrong format.
    {
        const auto legacy = texture_fetch(
            std::string("/caps/texture/") + session + "/?texture_id=" + asset, std::nullopt);
        check(legacy && legacy->texture == asset, "GetTexture resolves as a texture fetch");

        const auto path = std::string("/caps/assets/") + session + "/?texture_id=" + asset;
        const auto modern = texture_fetch(path, viewer_asset_request(path));
        check(modern && modern->texture == asset && modern->session == session,
              "ViewerAsset texture_id ALSO resolves as a texture fetch — the bug");

        // And a mesh must not: it has its own rendition kind, and treating it as
        // a texture would hand a viewer JPEG2000 where it expected geometry.
        const auto mesh_path = std::string("/caps/assets/") + session + "/?mesh_id=" + asset;
        check(!texture_fetch(mesh_path, viewer_asset_request(mesh_path)),
              "ViewerAsset mesh_id is not a texture fetch");

        const auto wearable = std::string("/caps/assets/") + session + "/?bodypart_id=" + asset;
        check(!texture_fetch(wearable, viewer_asset_request(wearable)),
              "ViewerAsset bodypart_id is not a texture fetch");
    }

    // --- Session and visit extraction, used by the seed and event queues.
    {
        check(capability_session(std::string("/caps/seed/") + session, "/caps/seed/") == session,
              "session with no visit");
        check(capability_session(std::string("/caps/seed/") + session + "/" + asset,
                                 "/caps/seed/") == session,
              "session with a visit");
        check(capability_session(std::string("/caps/seed/") + session + "/not-a-uuid",
                                 "/caps/seed/").empty(),
              "a malformed visit rejects the whole path rather than ignoring the visit");
        check(capability_visit(std::string("/caps/event/") + session + "/" + asset,
                               "/caps/event/") == asset,
              "visit extracted");
        check(capability_visit(std::string("/caps/event/") + session, "/caps/event/").empty(),
              "no visit present");
        check(capability_session("/caps/seed/", "/caps/seed/").empty(), "empty session");
    }

    if (failures != 0) {
        std::cerr << failures << " capability path check(s) failed\n";
        return 1;
    }
    std::cerr << "capability path parsing OK (both capabilities agree a texture is a texture)\n";
    return 0;
}
