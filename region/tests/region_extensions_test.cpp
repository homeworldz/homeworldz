// Negotiated region extensions (ADR 0032).
//
// The production registry is deliberately empty until an extension's
// implementation lands, so these tests supply their own extensions to exercise
// the mechanism, and separately assert that a region serving none behaves exactly
// as it did before extensions existed.
#include "homeworldz/region_extensions.h"
#include "homeworldz/viewer_capabilities.h"
#include "homeworldz/viewer_protocol.h"

#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {
using namespace homeworldz::viewer;

bool contains(std::string_view haystack, std::string_view needle) {
    return haystack.find(needle) != std::string_view::npos;
}

std::vector<RegionExtension> test_extensions() {
    return {
        RegionExtension{"HomeworldzTestGeometry", 2,
                        {{"TestGeometryMesh", "/caps/ext/test-geometry/"},
                         {"TestGeometryLOD", "/caps/ext/test-geometry-lod/"}}},
        RegionExtension{"HomeworldzTestTransport", 1,
                        {{"TestTransportSession", "/caps/ext/test-transport/"}}},
    };
}

std::string requested_body(const std::vector<std::string>& names) {
    std::string body = "<?xml version=\"1.0\"?><llsd><array>";
    for (const auto& name : names) body += "<string>" + name + "</string>";
    return body + "</array></llsd>";
}

// Count non-overlapping occurrences of a tag.
std::size_t count_of(std::string_view haystack, std::string_view needle) {
    std::size_t total = 0;
    for (std::size_t at = haystack.find(needle); at != std::string_view::npos;
         at = haystack.find(needle, at + needle.size()))
        ++total;
    return total;
}

// Substring assertions cannot catch an unbalanced document, and a malformed
// SimulatorFeatures reply would reach a live viewer, so check the structure too.
bool well_formed_llsd(std::string_view document) {
    return count_of(document, "<map>") == count_of(document, "</map>") &&
           count_of(document, "<array>") == count_of(document, "</array>") &&
           document.rfind("<?xml version=\"1.0\"?><llsd>", 0) == 0 &&
           document.size() > std::string_view("<?xml version=\"1.0\"?><llsd></llsd>").size() &&
           document.substr(document.size() - 7) == "</llsd>";
}

bool has_capability(const std::vector<ExtensionCapability>& granted, std::string_view name) {
    for (const auto& capability : granted)
        if (capability.name == name) return true;
    return false;
}
} // namespace

int main() {
    bool passed = true;

    // The region advertises capability, never intent: nothing is offered until an
    // extension is actually implemented.
    passed &= available_region_extensions().empty();

    // A client that requests nothing negotiates nothing, which is the entire
    // guarantee that a legacy viewer never sees an extension.
    passed &= negotiated_extension_capabilities(test_extensions(), {}).empty();

    // Naming one capability opts into its whole extension, and only that one.
    const auto geometry = negotiated_extension_capabilities(
        test_extensions(), {"TestGeometryMesh"});
    passed &= geometry.size() == 2;
    passed &= has_capability(geometry, "TestGeometryMesh");
    passed &= has_capability(geometry, "TestGeometryLOD");
    passed &= !has_capability(geometry, "TestTransportSession");

    // Requesting across extensions grants each of them.
    const auto both = negotiated_extension_capabilities(
        test_extensions(), {"TestGeometryLOD", "TestTransportSession"});
    passed &= both.size() == 3;

    // Degradation: an unknown name is ignored rather than rejected, so a newer
    // client talking to an older region simply gets less.
    const auto unknown = negotiated_extension_capabilities(
        test_extensions(), {"HomeworldzFutureThing", "TestTransportSession"});
    passed &= unknown.size() == 1;
    passed &= has_capability(unknown, "TestTransportSession");
    passed &= negotiated_extension_capabilities(test_extensions(), {"NothingWeKnow"}).empty();

    // Withdrawal: an extension absent from the available set is not granted even
    // to a client that still asks for it. Its stale capability URL then 404s.
    passed &= negotiated_extension_capabilities({}, {"TestGeometryMesh"}).empty();
    const auto withdrawn = negotiated_extension_capabilities(
        {test_extensions()[1]}, {"TestGeometryMesh", "TestTransportSession"});
    passed &= withdrawn.size() == 1;
    passed &= has_capability(withdrawn, "TestTransportSession");

    // Seed request parsing.
    const auto parsed = parse_requested_capabilities(
        requested_body({"EventQueueGet", "TestGeometryMesh"}));
    passed &= parsed.size() == 2 && parsed[0] == "EventQueueGet" &&
              parsed[1] == "TestGeometryMesh";
    // Firestorm's login seed sends no body at all.
    passed &= parse_requested_capabilities("").empty();
    passed &= parse_requested_capabilities("<llsd><map><key>x</key></map></llsd>").empty();
    passed &= parse_requested_capabilities("<llsd><array/></llsd>").empty();
    // A truncated body yields what it can rather than reading past the array.
    passed &= parse_requested_capabilities("<llsd><array><string>Broken").empty();
    // Untrusted input is bounded: an over-long name is dropped, and the list stops
    // at the cap rather than growing with the request.
    passed &= parse_requested_capabilities(
                  requested_body({std::string(max_capability_name_length + 1, 'x')})).empty();
    std::vector<std::string> flood;
    for (std::size_t index = 0; index < max_requested_capabilities + 32; ++index)
        flood.push_back("Cap" + std::to_string(index));
    passed &= parse_requested_capabilities(requested_body(flood)).size() ==
              max_requested_capabilities;

    // SimulatorFeatures advertises the mechanism and its map version even with
    // nothing to offer, so a client can tell an extension-aware region from one
    // that predates the mechanism.
    const auto bare = simulator_features_xml({.map_server_url = "https://grid.example/map/"});
    passed &= contains(bare, "<key>HomeworldzExtensions</key><map><key>version</key>"
                             "<integer>1</integer><key>extensions</key><map/></map>");
    // OpenSimExtras is untouched, and stays first.
    passed &= contains(bare, "<key>OpenSimExtras</key><map><key>currency</key><string>C$</string>");
    passed &= contains(bare, "<key>map-server-url</key><string>https://grid.example/map/</string>");
    passed &= bare.find("<key>OpenSimExtras</key>") < bare.find("<key>HomeworldzExtensions</key>");

    // Feature advertisement. Each flag must match behavior the region actually
    // implements, so these assert the honest answer rather than a hopeful one.
    // Implemented and therefore advertised: prim and none physics shapes, and
    // physics materials, which reach the Jolt body as friction and restitution.
    passed &= contains(bare, "<key>PhysicsShapeTypes</key><map>"
                             "<key>convex</key><boolean>0</boolean>"
                             "<key>none</key><boolean>1</boolean>"
                             "<key>prim</key><boolean>1</boolean></map>");
    passed &= contains(bare, "<key>PhysicsMaterialsEnabled</key><boolean>1</boolean>");
    // Not implemented, and advertised as a definite no rather than omitted: mesh
    // assets, dynamic pathfinding, and hover height, which the region emits in
    // AvatarAppearance but never accepts an update for.
    passed &= contains(bare, "<key>MeshRezEnabled</key><boolean>0</boolean>");
    passed &= contains(bare, "<key>MeshUploadEnabled</key><boolean>0</boolean>");
    passed &= contains(bare, "<key>MeshXferEnabled</key><boolean>0</boolean>");
    passed &= contains(bare, "<key>DynamicPathfindingEnabled</key><boolean>0</boolean>");
    passed &= contains(bare, "<key>AvatarHoverHeightEnabled</key><boolean>0</boolean>");
    // The Export permission bit is enforced in the permission core.
    passed &= contains(bare, "<key>ExportSupported</key><boolean>1</boolean>");
    // Advertised chat ranges are the enforced ones, taken from the same
    // constants the relay uses, so the two cannot drift apart.
    passed &= contains(bare, "<key>whisper-range</key><integer>10</integer>");
    passed &= contains(bare, "<key>say-range</key><integer>20</integer>");
    passed &= contains(bare, "<key>shout-range</key><integer>100</integer>");
    passed &= chat_range(chat_type_whisper) == 10.0;
    passed &= chat_range(chat_type_normal) == 20.0;
    passed &= chat_range(chat_type_shout) == 100.0;
    // An unrecognized chat type is never louder than normal speech.
    passed &= chat_range(0x7f) == chat_range(chat_type_normal);

    // A region that later implements one flips its flag without touching the
    // rest of the advertisement.
    const auto with_mesh = simulator_features_xml({.mesh = true});
    passed &= contains(with_mesh, "<key>MeshRezEnabled</key><boolean>1</boolean>");
    passed &= contains(with_mesh, "<key>MeshUploadEnabled</key><boolean>1</boolean>");
    passed &= contains(with_mesh, "<key>PhysicsMaterialsEnabled</key><boolean>1</boolean>");

    // An advertised extension carries its own version and the capability names a
    // client names to opt in.
    const auto advertised = simulator_features_xml(
        {.map_server_url = "https://grid.example/map/", .extensions = test_extensions()});
    passed &= contains(advertised, "<key>HomeworldzTestGeometry</key><map><key>version</key>"
                                   "<integer>2</integer><key>capabilities</key><array>"
                                   "<string>TestGeometryMesh</string>"
                                   "<string>TestGeometryLOD</string></array></map>");
    passed &= contains(advertised, "<key>HomeworldzTestTransport</key><map><key>version</key>"
                                   "<integer>1</integer>");

    // Legacy non-regression: with no extension negotiated, the seed reply is
    // byte-identical to the one built before extensions existed. This is the
    // guarantee ADR 0032 rests on, so it is asserted on equality rather than by
    // spot-checking keys.
    const auto baseline = seed_capability_xml(
        "http://region.example:42001/", "http://grid.example:42000/", "session-id");
    const auto negotiated_none = seed_capability_xml(
        "http://region.example:42001/", "http://grid.example:42000/", "session-id", {},
        negotiated_extension_capabilities(available_region_extensions(),
                                          parse_requested_capabilities("")));
    passed &= baseline == negotiated_none;
    // Even a client that asks for extensions gets the baseline reply while the
    // region serves none.
    passed &= baseline == seed_capability_xml(
        "http://region.example:42001/", "http://grid.example:42000/", "session-id", {},
        negotiated_extension_capabilities(
            available_region_extensions(),
            parse_requested_capabilities(requested_body({"TestGeometryMesh"}))));
    passed &= !contains(baseline, "HomeworldzExtensions");
    passed &= !contains(baseline, "/caps/ext/");

    // A negotiated capability appears in the seed reply at its own path, appended
    // after the baseline set so no baseline entry moves or changes.
    const auto granted = seed_capability_xml(
        "http://region.example:42001/", "http://grid.example:42000/", "session-id", {},
        negotiated_extension_capabilities(test_extensions(), {"TestTransportSession"}));
    passed &= contains(granted, "<key>TestTransportSession</key><uri>"
                                "http://region.example:42001/caps/ext/test-transport/session-id</uri>");
    passed &= granted.rfind("<key>LibraryAPIv3</key>") <
              granted.rfind("<key>TestTransportSession</key>");
    passed &= contains(granted, "<key>EventQueueGet</key><uri>"
                                "http://region.example:42001/caps/event/session-id</uri>");
    // The baseline reply is a strict prefix of the negotiated one up to the
    // closing map, which is what "additive" means concretely.
    const auto baseline_body = baseline.substr(0, baseline.size() - std::string("</map></llsd>").size());
    passed &= granted.rfind(baseline_body, 0) == 0;

    // A visit id still routes the event queue correctly alongside an extension.
    const auto with_visit = seed_capability_xml(
        "http://region.example:42001/", "http://grid.example:42000/", "session-id",
        "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa",
        negotiated_extension_capabilities(test_extensions(), {"TestTransportSession"}));
    passed &= contains(with_visit, "<key>EventQueueGet</key><uri>http://region.example:42001/caps/event/"
                                   "session-id/aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa</uri>");
    passed &= contains(with_visit, "<key>TestTransportSession</key>");

    // ObjectPhysicsProperties: the viewer's only source for the Extra Physics
    // fields. Defaults must be the region's real ones, because the viewer posts
    // the whole set back when the creator edits any single field — so a zero here
    // becomes a zero in the scene.
    const ObjectPhysicsProperties defaults{};
    passed &= defaults.density == 1000.0 && defaults.friction == 0.6 &&
              defaults.restitution == 0.5 && defaults.gravity_multiplier == 1.0;
    const auto physics = object_physics_properties_event_xml(
        {42, 2, 1000.0, 0.6, 0.5, 1.0});
    passed &= contains(physics, "<key>message</key><string>ObjectPhysicsProperties</string>");
    passed &= contains(physics, "<key>LocalID</key><integer>42</integer>");
    passed &= contains(physics, "<key>PhysicsShapeType</key><integer>2</integer>");
    passed &= contains(physics, "<key>Density</key><real>1000.000000</real>");
    passed &= contains(physics, "<key>Friction</key><real>0.600000</real>");
    passed &= contains(physics, "<key>Restitution</key><real>0.500000</real>");
    passed &= contains(physics, "<key>GravityMultiplier</key><real>1.000000</real>");
    passed &= contains(physics, "<key>ObjectData</key><array><map>");
    passed &= well_formed_llsd("<?xml version=\"1.0\"?><llsd>" + physics + "</llsd>");
    // A zeroed set is representable, since that is what a viewer sends when it has
    // never been told otherwise — the regression this event exists to prevent.
    const auto zeroed = object_physics_properties_event_xml({1, 0, 0.0, 0.0, 0.0, 0.0});
    passed &= contains(zeroed, "<key>GravityMultiplier</key><real>0.000000</real>");

    // Structure, not just contents: an unbalanced map or array would still
    // satisfy every substring assertion above but break a real viewer.
    passed &= well_formed_llsd(bare);
    passed &= well_formed_llsd(advertised);
    passed &= well_formed_llsd(baseline);
    passed &= well_formed_llsd(granted);
    passed &= well_formed_llsd(simulator_features_xml({}));

    if (!passed) {
        std::cerr << "region extension negotiation checks failed\n";
        std::cerr << "SimulatorFeatures: " << bare << '\n';
    }
    return passed ? 0 : 1;
}
