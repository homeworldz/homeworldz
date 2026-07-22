#include "homeworldz/wearable.h"

#include <iostream>
#include <string>

using homeworldz::viewer::format_uuid;
using homeworldz::viewer::parse_wearable;
using homeworldz::viewer::WearableType;

namespace {

// A representative LLWearable clothing asset (lower-body "pants"), including the
// permissions and sale_info blocks the parser must skip. Tabs are the native
// indentation in these assets.
const char* const kPants =
    "LLWearable version 22\n"
    "Test Pants\n"
    "\tpermissions 0\n"
    "\t{\n"
    "\t\tbase_mask\t7fffffff\n"
    "\t\towner_mask\t7fffffff\n"
    "\t\tgroup_mask\t00000000\n"
    "\t\teveryone_mask\t00000000\n"
    "\t\tnext_owner_mask\t00082000\n"
    "\t\tcreator_id\t11111111-1111-1111-1111-111111111111\n"
    "\t\towner_id\t22222222-2222-2222-2222-222222222222\n"
    "\t\tlast_owner_id\t33333333-3333-3333-3333-333333333333\n"
    "\t\tgroup_id\t00000000-0000-0000-0000-000000000000\n"
    "\t}\n"
    "\tsale_info\t0\n"
    "\t{\n"
    "\t\tsale_type\tnot\n"
    "\t\tsale_price\t10\n"
    "\t}\n"
    "type 5\n"
    "parameters 3\n"
    "\t80 0.5\n"
    "\t812 0\n"
    "\t1017 0\n"
    "textures 1\n"
    "\t17 89556747-24cb-43ed-920b-47caed15465f\n";

bool expect(bool ok, const char* what) {
    if (!ok) std::cerr << "FAIL: " << what << '\n';
    return ok;
}

}  // namespace

int main() {
    bool ok = true;

    auto parsed = parse_wearable(kPants);
    if (!parsed) {
        std::cerr << "parse_wearable returned nullopt for a valid asset\n";
        return 1;
    }

    ok &= expect(parsed->version == 22, "version == 22");
    ok &= expect(parsed->name == "Test Pants", "name == \"Test Pants\"");
    ok &= expect(parsed->type == WearableType::Pants, "type == Pants (5)");

    ok &= expect(parsed->parameters.size() == 3, "3 parameters");
    ok &= expect(parsed->parameters.count(80) && parsed->parameters.at(80) == 0.5,
                 "parameter 80 == 0.5");
    ok &= expect(parsed->parameters.count(812) && parsed->parameters.at(812) == 0.0,
                 "parameter 812 == 0");

    ok &= expect(parsed->textures.size() == 1, "1 texture");
    ok &= expect(parsed->textures.count(17) == 1, "texture index 17 present");
    if (parsed->textures.count(17)) {
        ok &= expect(format_uuid(parsed->textures.at(17)) ==
                         "89556747-24cb-43ed-920b-47caed15465f",
                     "texture 17 UUID round-trips");
    }

    // A non-wearable blob must be rejected.
    ok &= expect(!parse_wearable("not a wearable\nat all\n").has_value(),
                 "reject non-LLWearable input");
    // Missing type must be rejected.
    ok &= expect(!parse_wearable("LLWearable version 22\nNoType\n").has_value(),
                 "reject wearable without a type");

    if (!ok) return 1;
    std::cerr << "wearable parse OK\n";
    return 0;
}
