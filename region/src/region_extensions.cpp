#include "homeworldz/region_extensions.h"

#include <algorithm>

namespace homeworldz::viewer {

std::vector<RegionExtension> available_region_extensions() {
    // No extension implementation exists yet, so the region serves none. See the
    // header: this list grows with the features themselves, never ahead of them.
    return {};
}

std::vector<ExtensionCapability> negotiated_extension_capabilities(
    const std::vector<RegionExtension>& available,
    const std::vector<std::string>& requested) {
    std::vector<ExtensionCapability> granted;
    for (const auto& extension : available) {
        // Whole-extension grant: any one of its capability names being requested
        // opts the client into the extension, so it never receives a fragment it
        // cannot use.
        const bool wanted = std::any_of(
            extension.capabilities.begin(), extension.capabilities.end(),
            [&requested](const ExtensionCapability& capability) {
                return std::find(requested.begin(), requested.end(), capability.name) !=
                       requested.end();
            });
        if (!wanted) continue;
        granted.insert(granted.end(), extension.capabilities.begin(),
                       extension.capabilities.end());
    }
    return granted;
}

} // namespace homeworldz::viewer
