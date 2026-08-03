// Material identity and LLSD conversion.
//
// What these can and cannot prove is worth stating, because the distinction is
// the whole reason the capability logs what a viewer sends. The identity, the
// defaults, the clamping and the unknown-key reporting are properties of this
// code and are settled here. Whether the *key names* match what Firestorm uses
// is not: a round-trip through this file would agree with itself either way.
// That question is answered by the log line the capability writes on the first
// real materials edit, and until then wire compatibility is unproven.
#include "homeworldz/llsd_binary.h"
#include "homeworldz/render_material.h"

#include <iostream>
#include <set>
#include <string>

using homeworldz::llsd::parse_binary;
using homeworldz::llsd::to_binary;
using homeworldz::llsd::Value;
using homeworldz::material::format_id;
using homeworldz::material::from_llsd;
using homeworldz::material::identify;
using homeworldz::material::RenderMaterial;
using homeworldz::material::to_llsd;
using homeworldz::material::wire_scale;

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
    // --- Identity is the definition's own hash, so equal materials converge.
    {
        RenderMaterial first;
        first.normal_map = "26700e50-492d-4243-9513-1905e8109e2b";
        first.specular_exponent = 128;
        RenderMaterial second = first;
        check(identify(first) == identify(second), "identical definitions share an id");

        RenderMaterial different = first;
        different.specular_exponent = 129;
        check(identify(first) != identify(different),
              "one changed byte changes the id");

        // A field that is set to its own default must not read as a different
        // material from one where it was omitted, or every viewer that spells
        // its defaults out creates duplicate records.
        RenderMaterial defaulted;
        defaulted.normal_repeat_x = wire_scale;
        check(identify(RenderMaterial{}) == identify(defaulted),
              "an explicit default is the same material as an omitted one");

        // The two maps must not be interchangeable: a normal map assigned as a
        // specular map is a different material, and a hash that ignored which
        // slot a texture sat in would silently merge them.
        RenderMaterial as_normal;
        as_normal.normal_map = "26700e50-492d-4243-9513-1905e8109e2b";
        RenderMaterial as_specular;
        as_specular.specular_map = "26700e50-492d-4243-9513-1905e8109e2b";
        check(identify(as_normal) != identify(as_specular),
              "the same texture in a different slot is a different material");

        const auto text = format_id(identify(first));
        check(text.size() == 36 && text[8] == '-' && text[13] == '-',
              "an id formats as a hyphenated 16-byte value");

        // The id of a material with every field defaulted, recorded as a golden
        // value. Not decoration: a parser that reads the wrong part of a
        // document registers *this* material and reports success, which is
        // precisely what happened on 2026-07-31 when the envelope was parsed as
        // a definition. Seeing this id in a log means nothing was read.
        const auto empty_id = format_id(identify(RenderMaterial{}));
        std::cerr << "  id of an all-default material: " << empty_id << '\n';
        check(empty_id == "3fca6644-2ec9-c38e-cf83-0cd3c89fb68e",
              "the all-default material's id is the known value — if this fails the"
              " canonical form changed, and every stored id is stale");
    }

    // --- Defaults. Repeats default to one, not zero: a material that arrived
    // with no repeat and got zero would render nothing at all.
    {
        Value empty;
        empty.type = Value::Type::map;
        const auto parsed = from_llsd(empty);
        check(parsed.ok, "an empty map is a valid material of all defaults");
        check(parsed.material.normal_repeat_x == wire_scale &&
                  parsed.material.normal_repeat_y == wire_scale &&
                  parsed.material.specular_repeat_x == wire_scale &&
                  parsed.material.specular_repeat_y == wire_scale,
              "repeats default to one rather than zero");
        check(parsed.material.specular_colour == std::array<std::uint8_t, 4>{255, 255, 255, 255},
              "specular colour defaults to white");
        check(parsed.material.normal_map.empty() && parsed.material.specular_map.empty(),
              "no maps by default");

        check(!from_llsd(Value{}).ok, "a non-map is not a material");
    }

    // --- The nil UUID means "no map", and must not be stored as a texture id
    // that something will later try to fetch.
    {
        auto material = to_llsd(RenderMaterial{});
        const auto parsed = from_llsd(material);
        check(parsed.material.normal_map.empty(),
              "the nil uuid round-trips back to no map, not to a fetchable id");
    }

    // --- Unknown keys are reported rather than dropped. This is the mechanism
    // that will tell us if the key names in render_material.cpp are wrong.
    {
        Value map;
        map.type = Value::Type::map;
        Value number;
        number.type = Value::Type::integer;
        number.integer = 1;
        map.members.emplace_back("NormMap", [] {
            Value id;
            id.type = Value::Type::uuid;
            id.text = "26700e50-492d-4243-9513-1905e8109e2b";
            return id;
        }());
        map.members.emplace_back("SomethingElse", number);
        map.members.emplace_back("AnotherThing", number);
        const auto parsed = from_llsd(map);
        check(parsed.ok, "a material with unknown keys still parses");
        const std::set<std::string> unknown(parsed.unknown_keys.begin(), parsed.unknown_keys.end());
        check(unknown == std::set<std::string>{"SomethingElse", "AnotherThing"},
              "every unrecognised key is reported");
        check(parsed.material.normal_map == "26700e50-492d-4243-9513-1905e8109e2b",
              "recognised keys are still read alongside unknown ones");
    }

    // --- Out-of-range bytes clamp rather than wrap. A specular exponent of 300
    // wrapping to 44 would be a wrong material that looked like a valid one.
    {
        Value map;
        map.type = Value::Type::map;
        Value big;
        big.type = Value::Type::integer;
        big.integer = 300;
        map.members.emplace_back("SpecExp", big);
        Value negative;
        negative.type = Value::Type::integer;
        negative.integer = -5;
        map.members.emplace_back("EnvIntensity", negative);
        const auto parsed = from_llsd(map);
        check(parsed.material.specular_exponent == 255, "an over-range byte clamps to 255");
        check(parsed.material.environment_intensity == 0, "a negative byte clamps to 0");
    }

    // --- Through LLSD binary, which is how it actually travels.
    {
        RenderMaterial material;
        material.normal_map = "26700e50-492d-4243-9513-1905e8109e2b";
        material.specular_map = "9a2248ec-28de-40a0-a459-9873e1f7464c";
        material.normal_offset_x = 2500;
        material.specular_rotation = 1234;
        material.specular_colour = {200, 150, 100, 255};
        material.specular_exponent = 64;
        material.environment_intensity = 32;
        material.alpha_mask_cutoff = 128;
        material.diffuse_alpha_mode = 1;

        const auto encoded = to_binary(to_llsd(material));
        const auto decoded = parse_binary(encoded);
        check(decoded.has_value(), "a material survives LLSD binary");
        if (decoded) {
            const auto parsed = from_llsd(*decoded);
            check(parsed.ok && parsed.material == material,
                  "every field survives the trip through binary LLSD");
            check(parsed.unknown_keys.empty(),
                  "our own output contains no key we do not recognise");
            check(identify(parsed.material) == identify(material),
                  "identity survives serialization");
        }
    }


    // --- Definitions are found by content, not position. This is the fix for
    // 2026-07-31: Firestorm wraps definitions in a "FullMaterialsPerFace" array
    // of per-face entries, and a parser that assumed the document was either one
    // definition or a flat list of them parsed the *envelope* as a material,
    // registered an all-default one, and answered 200. It looked like success in
    // every log line except the unknown-key warning.
    {
        const auto make_material = [] {
            Value m;
            m.type = Value::Type::map;
            Value id;
            id.type = Value::Type::uuid;
            id.text = "26700e50-492d-4243-9513-1905e8109e2b";
            m.members.emplace_back("SpecMap", id);
            return m;
        };
        // The envelope Firestorm actually sends, as far as its outer key is known.
        Value entry;
        entry.type = Value::Type::map;
        Value face;
        face.type = Value::Type::integer;
        face.integer = 0;
        entry.members.emplace_back("Face", face);
        entry.members.emplace_back("Material", make_material());
        Value list;
        list.type = Value::Type::array;
        list.elements.push_back(entry);
        Value envelope;
        envelope.type = Value::Type::map;
        envelope.members.emplace_back("FullMaterialsPerFace", list);

        const auto found = homeworldz::material::find_materials(envelope);
        check(found.size() == 1, "one definition found inside the per-face envelope");
        if (found.size() == 1) {
            const auto parsed = from_llsd(*found[0]);
            check(parsed.material.specular_map == "26700e50-492d-4243-9513-1905e8109e2b",
                  "the definition's own fields are read, not the envelope's");
            check(identify(parsed.material) != identify(RenderMaterial{}),
                  "and the result is NOT the all-default material - the symptom of the bug");
        }
        // The envelope alone is not a definition, however deeply it is wrapped.
        check(!homeworldz::material::looks_like_material(envelope),
              "an envelope carrying no material field is not a material");
        check(homeworldz::material::find_materials(Value{}).empty(),
              "a document with no definitions yields none rather than one of defaults");
        // A bare definition still works: the search must not require a wrapper.
        const auto bare = homeworldz::material::find_materials(make_material());
        check(bare.size() == 1, "a bare definition is still found");
    }

    if (failures != 0) {
        std::cerr << failures << " render material check(s) failed\n";
        return 1;
    }
    std::cerr << "render material identity and LLSD OK"
                 " (key names still unverified against a viewer)\n";
    return 0;
}
