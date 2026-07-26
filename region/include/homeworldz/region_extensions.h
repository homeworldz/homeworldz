// Negotiated region extensions for the first-party client (ADR 0032).
//
// Baseline Second Life protocol semantics are untouched. New region behavior is
// negotiated per feature through two existing mechanisms used as intended: the
// region advertises what it can serve in the `SimulatorFeatures` capability, and
// a client opts in by naming an extension's capabilities in its seed request.
// A viewer that knows nothing about an extension never asks for it and therefore
// never sees it, which is what keeps every extension invisible to Firestorm and
// its peers (ADR 0016).
//
// This is the single mechanism for adding an extension. An extension must not
// introduce a new negotiation of its own, change a baseline message, or alter a
// wire format.
#ifndef HOMEWORLDZ_REGION_EXTENSIONS_H
#define HOMEWORLDZ_REGION_EXTENSIONS_H

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace homeworldz::viewer {

// Bounds on a seed request's capability list. The body arrives from a viewer and
// is therefore untrusted: these cap how much a single request can make the region
// allocate. Both sit far above any real client's list.
inline constexpr std::size_t max_requested_capabilities = 0x80;
inline constexpr std::size_t max_capability_name_length = 0x40;

// One capability an extension grants. The name is the key a client names in its
// seed request and receives back in the seed reply; the path is the region-local
// prefix that serves it, to which the session id is appended.
struct ExtensionCapability {
    std::string name;
    std::string path;
};

// An extension this region can serve. The version is the extension's own, so a
// client can tell which revision of a feature it is talking to; it is unrelated
// to the map version below.
struct RegionExtension {
    std::string name;
    int version{1};
    std::vector<ExtensionCapability> capabilities;
};

// Version of the extension map's own shape, bumped when the structure of the
// advertisement changes rather than when an extension changes. A client that
// does not recognize this number should treat the map as opaque and negotiate
// nothing, which degrades it to the baseline protocol.
inline constexpr int extension_map_version = 1;

// Extensions this region can actually serve right now.
//
// Deliberately empty: the map advertises capability, never intent. ADR 0032
// anticipates modern asset formats at rest, server-side prim meshing, and a
// browser-reachable transport, but none of their implementations exist yet, and
// advertising one before it works would promise a client something the region
// cannot deliver. Each lands here as part of its own implementation.
std::vector<RegionExtension> available_region_extensions();

// Resolve a seed request against what the region can serve: the capabilities of
// every extension that is both available and explicitly requested.
//
// The degradation rules live here. A requested name the region does not know is
// ignored rather than rejected, so a newer client simply gets less. An extension
// that has been withdrawn is absent from `available` and is therefore not granted
// even to a client that still asks for it, whose stale capability URL then 404s.
// An extension is granted only in full: a client naming one of its capabilities
// receives them all, because a partially negotiated extension has no defined
// meaning.
std::vector<ExtensionCapability> negotiated_extension_capabilities(
    const std::vector<RegionExtension>& available,
    const std::vector<std::string>& requested);

} // namespace homeworldz::viewer

#endif
