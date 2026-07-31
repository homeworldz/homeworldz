// The seed reply lists every capability a viewer is allowed to use, and a
// capability the region implements but does not advertise is not reachable at
// all. That is not hypothetical here: RenderMaterials was missing from this
// list, so a viewer had nowhere to register a material and every assignment was
// discarded silently (found live 2026-07-29). GetMesh had the same shape of
// failure earlier - implemented, unadvertised, mesh prims invisible.
//
// So each capability is asserted present *with the path its handler matches*,
// because a name advertised against the wrong path fails exactly as completely
// as one not advertised at all, and reads as correct in the reply.
#include "homeworldz/viewer_capabilities.h"

#include <iostream>
#include <string>

namespace {

int failures = 0;
std::string seed;

void check(bool condition, const std::string& what) {
    if (!condition) {
        std::cerr << "FAIL: " << what << '\n';
        ++failures;
    }
}

void advertises(const std::string& name, const std::string& path) {
    const auto expected = "<key>" + name + "</key><uri>http://region.example/caps/" + path;
    check(seed.find(expected) != std::string::npos,
          name + " advertised at /caps/" + path);
}

} // namespace

int main() {
    constexpr const char* session = "60e9ead5-8827-4c0b-8f3d-2da9eaff2b6a";
    seed = homeworldz::viewer::seed_capability_xml("http://region.example/",
                                                  "http://grid.example", session, "", {});

    check(seed.rfind("<?xml", 0) == 0, "the seed is an XML document");
    check(seed.find("</llsd>") != std::string::npos, "the seed closes its llsd element");

    // Region-served capabilities, each against the prefix its handler parses.
    advertises("EventQueueGet", "event/");
    advertises("GetTexture", "texture/");
    advertises("ViewerAsset", "assets/");
    // Mesh goes through the same endpoint as ViewerAsset: the handler reads any
    // <type>_id= query. A viewer granted neither renders mesh prims invisible.
    advertises("GetMesh", "assets/");
    advertises("GetMesh2", "assets/");
    advertises("SimulatorFeatures", "simulator-features/");
    advertises("EnvironmentSettings", "environment/");
    advertises("RemoteParcelRequest", "remote-parcel/");
    advertises("UploadBakedTexture", "upload-baked/");
    advertises("NewFileAgentInventory", "upload-file/");
    advertises("MeshUploadFlag", "mesh-upload-flag/");
    // The one this file was written for.
    advertises("RenderMaterials", "render-materials/");
    advertises("UpdateNotecardAgentInventory", "update-notecard/");
    advertises("UpdateScriptAgentInventory", "update-script/");
    advertises("UpdateGestureAgentInventory", "update-gesture/");

    // Grid-served capabilities point at the grid, not the region. Pointing an
    // inventory capability at the region would 404 every fetch.
    check(seed.find("<key>FetchInventoryDescendents2</key><uri>http://grid.example/caps/inventory/") !=
              std::string::npos,
          "inventory capabilities point at the grid");

    // The session id travels in every URL: a capability URL without it cannot
    // authorize, and the handlers derive the session from the path.
    check(seed.find(session) != std::string::npos, "the session id is present in the URLs");

    // A trailing slash on the configured endpoint must not produce a double
    // slash, which some viewers normalize and others do not.
    check(seed.find("//caps/") == std::string::npos,
          "a trailing slash on the endpoint does not double up");

    // Negotiated extensions append after the baseline, so a client that
    // negotiated none receives exactly the pre-extension reply.
    const auto with_extension = homeworldz::viewer::seed_capability_xml(
        "http://region.example", "http://grid.example", session, "",
        {{"HomeworldzThing", "/caps/thing/"}});
    check(with_extension.find("<key>HomeworldzThing</key><uri>http://region.example/caps/thing/") !=
              std::string::npos,
          "an extension capability is appended");
    check(with_extension.size() > seed.size(), "extensions add to the baseline rather than replace it");

    if (failures != 0) {
        std::cerr << failures << " seed capability check(s) failed\n";
        return 1;
    }
    std::cerr << "seed capabilities OK (every implemented capability advertised at its own path)\n";
    return 0;
}
