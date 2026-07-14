#include "homeworldz/object_asset.h"

#include <string>

int main() {
    const std::string json = R"({"format":"homeworldz-object-v1","creatorId":"10000000-0000-4000-8000-000000000001","name":"Prim2","scale":[0.500000,0.500000,0.500000],"rotation":[0.000000,0.000000,0.382683],"description":"Tall \"box\"","material":3,"basePermissions":647168})";
    const auto bytes = std::span(reinterpret_cast<const std::byte*>(json.data()), json.size());
    const auto asset = homeworldz::asset::parse_object_asset(bytes);
    if (!asset || asset->scale.x != 0.5 || asset->rotation.z != 0.382683 || asset->material != 3 ||
        asset->description != "Tall \"box\"")
        return 1;
    const std::string invalid = R"({"format":"homeworldz-object-v1","scale":[0,1,1],"rotation":[0,0,0],"description":"","material":3})";
    if (homeworldz::asset::parse_object_asset(
            std::span(reinterpret_cast<const std::byte*>(invalid.data()), invalid.size())))
        return 1;
    return 0;
}
