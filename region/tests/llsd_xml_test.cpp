#include "homeworldz/llsd_xml.h"

#include <string>

using homeworldz::llsd::Value;
using homeworldz::llsd::parse_xml;

int main() {
    // A document exercising every element the mesh model upload uses: nested
    // maps, arrays, base64 binary, integers, reals, strings, uuid, boolean,
    // self-closing forms, and entity decoding.
    const std::string document =
        "<?xml version=\"1.0\"?>\n"
        "<llsd><map>"
        "<key>name</key><string>A &amp; B &lt;model&gt;</string>"
        "<key>folder_id</key><uuid>2aa7faa9-38de-43f5-8bc5-35b8d68a64b4</uuid>"
        "<key>count</key><integer>3</integer>"
        "<key>price</key><real>1.5</real>"
        "<key>enabled</key><boolean>true</boolean>"
        "<key>nothing</key><undef/>"
        "<key>empty_text</key><string/>"
        "<key>asset_resources</key><map>"
        "<key>mesh_list</key><array>"
        "<binary encoding=\"base64\">aGVsbG8=</binary>"
        "<binary/>"
        "</array>"
        "<key>instance_list</key><array><map>"
        "<key>position</key><array><real>1</real><real>-2.5</real><real>0</real></array>"
        "<key>mesh</key><integer>0</integer>"
        "</map></array>"
        "<key>empty_array</key><array/>"
        "<key>empty_map</key><map/>"
        "</map>"
        "</map></llsd>";
    const auto parsed = parse_xml(document);
    if (!parsed || parsed->type != Value::Type::map) return 1;
    const auto* name = parsed->find("name");
    if (name == nullptr || name->text != "A & B <model>") return 2;
    const auto* folder = parsed->find("folder_id");
    if (folder == nullptr || folder->type != Value::Type::uuid ||
        folder->text != "2aa7faa9-38de-43f5-8bc5-35b8d68a64b4") return 3;
    if (parsed->find("count") == nullptr || parsed->find("count")->as_integer() != 3) return 4;
    if (parsed->find("price") == nullptr || parsed->find("price")->as_real() != 1.5) return 5;
    if (parsed->find("enabled") == nullptr || !parsed->find("enabled")->boolean) return 6;
    if (parsed->find("nothing") == nullptr ||
        parsed->find("nothing")->type != Value::Type::undefined) return 7;
    if (parsed->find("empty_text") == nullptr ||
        !parsed->find("empty_text")->text.empty()) return 8;
    const auto* resources = parsed->find("asset_resources");
    if (resources == nullptr || resources->type != Value::Type::map) return 9;
    const auto* meshes = resources->find("mesh_list");
    if (meshes == nullptr || meshes->type != Value::Type::array ||
        meshes->elements.size() != 2) return 10;
    const auto& first = meshes->elements[0];
    if (first.type != Value::Type::binary || first.binary.size() != 5 ||
        first.binary[0] != std::byte{'h'} || first.binary[4] != std::byte{'o'}) return 11;
    if (meshes->elements[1].type != Value::Type::binary ||
        !meshes->elements[1].binary.empty()) return 12;
    const auto* instances = resources->find("instance_list");
    if (instances == nullptr || instances->elements.size() != 1) return 13;
    const auto* position = instances->elements[0].find("position");
    if (position == nullptr || position->elements.size() != 3 ||
        position->elements[1].as_real() != -2.5) return 14;
    if (resources->find("empty_array") == nullptr ||
        resources->find("empty_array")->type != Value::Type::array ||
        !resources->find("empty_array")->elements.empty()) return 15;
    if (resources->find("empty_map") == nullptr ||
        resources->find("empty_map")->type != Value::Type::map) return 16;

    // Refusals: junk, truncation, bad base64, unknown elements, and a missing
    // llsd wrapper all parse to nothing.
    if (parse_xml("not xml")) return 20;
    if (parse_xml("<llsd><map><key>a</key><integer>1</integer>")) return 21;
    if (parse_xml("<llsd><binary>!!!!</binary></llsd>")) return 22;
    if (parse_xml("<llsd><date>2026-07-29</date></llsd>")) return 23;
    if (parse_xml("<map><key>a</key><integer>1</integer></map>")) return 24;
    // A map key without a value is malformed, not an empty member.
    if (parse_xml("<llsd><map><key>a</key></map></llsd>")) return 25;
    return 0;
}
