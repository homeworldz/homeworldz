#include "homeworldz/render_material.h"

#include "homeworldz/sha256.h"

#include <algorithm>
#include <cstring>

namespace homeworldz::material {

namespace {

// The wire names, as this project reads the format. Grouped so the pairs stay
// visibly parallel: a normal field that has no specular counterpart is far more
// likely to be a mistake than a deliberate asymmetry.
constexpr const char* key_normal_map = "NormMap";
constexpr const char* key_normal_offset_x = "NormOffsetX";
constexpr const char* key_normal_offset_y = "NormOffsetY";
constexpr const char* key_normal_repeat_x = "NormRepeatX";
constexpr const char* key_normal_repeat_y = "NormRepeatY";
constexpr const char* key_normal_rotation = "NormRotation";
constexpr const char* key_specular_map = "SpecMap";
constexpr const char* key_specular_offset_x = "SpecOffsetX";
constexpr const char* key_specular_offset_y = "SpecOffsetY";
constexpr const char* key_specular_repeat_x = "SpecRepeatX";
constexpr const char* key_specular_repeat_y = "SpecRepeatY";
constexpr const char* key_specular_rotation = "SpecRotation";
constexpr const char* key_specular_colour = "SpecColor";
constexpr const char* key_specular_exponent = "SpecExp";
constexpr const char* key_environment_intensity = "EnvIntensity";
constexpr const char* key_alpha_mask_cutoff = "AlphaMaskCutoff";
constexpr const char* key_diffuse_alpha_mode = "DiffuseAlphaMode";

constexpr std::array<const char*, 17> known_keys{
    key_normal_map, key_normal_offset_x, key_normal_offset_y, key_normal_repeat_x,
    key_normal_repeat_y, key_normal_rotation, key_specular_map, key_specular_offset_x,
    key_specular_offset_y, key_specular_repeat_x, key_specular_repeat_y, key_specular_rotation,
    key_specular_colour, key_specular_exponent, key_environment_intensity,
    key_alpha_mask_cutoff, key_diffuse_alpha_mode};

llsd::Value integer_value(std::int32_t value) {
    llsd::Value out;
    out.type = llsd::Value::Type::integer;
    out.integer = value;
    return out;
}

llsd::Value uuid_value(const std::string& text) {
    llsd::Value out;
    out.type = llsd::Value::Type::uuid;
    out.text = text.empty() ? "00000000-0000-0000-0000-000000000000" : text;
    return out;
}

std::int32_t read_integer(const llsd::Value& map, const char* key, std::int32_t fallback) {
    const auto* member = map.find(key);
    return member != nullptr ? static_cast<std::int32_t>(member->as_integer(fallback)) : fallback;
}

std::uint8_t read_byte(const llsd::Value& map, const char* key, std::uint8_t fallback) {
    const auto* member = map.find(key);
    if (member == nullptr) return fallback;
    const auto value = member->as_integer(fallback);
    return static_cast<std::uint8_t>(std::clamp<std::int64_t>(value, 0, 255));
}

std::string read_uuid(const llsd::Value& map, const char* key) {
    const auto* member = map.find(key);
    if (member == nullptr) return {};
    if (member->text == "00000000-0000-0000-0000-000000000000") return {};
    return member->text;
}

// Every field, in a fixed order, appended as its wire representation. The
// identity must not depend on map ordering, key spelling, or anything a viewer
// chose to omit, so it is taken over the resolved struct rather than over the
// document that produced it.
std::vector<std::byte> canonical_form(const RenderMaterial& material) {
    std::vector<std::byte> out;
    const auto append_int = [&out](std::int32_t value) {
        const auto raw = static_cast<std::uint32_t>(value);
        out.push_back(static_cast<std::byte>((raw >> 24) & 0xff));
        out.push_back(static_cast<std::byte>((raw >> 16) & 0xff));
        out.push_back(static_cast<std::byte>((raw >> 8) & 0xff));
        out.push_back(static_cast<std::byte>(raw & 0xff));
    };
    const auto append_text = [&out, &append_int](const std::string& text) {
        append_int(static_cast<std::int32_t>(text.size()));
        for (const auto character : text) out.push_back(static_cast<std::byte>(character));
    };
    append_text(material.normal_map);
    append_int(material.normal_offset_x);
    append_int(material.normal_offset_y);
    append_int(material.normal_repeat_x);
    append_int(material.normal_repeat_y);
    append_int(material.normal_rotation);
    append_text(material.specular_map);
    append_int(material.specular_offset_x);
    append_int(material.specular_offset_y);
    append_int(material.specular_repeat_x);
    append_int(material.specular_repeat_y);
    append_int(material.specular_rotation);
    for (const auto component : material.specular_colour)
        out.push_back(static_cast<std::byte>(component));
    out.push_back(static_cast<std::byte>(material.specular_exponent));
    out.push_back(static_cast<std::byte>(material.environment_intensity));
    out.push_back(static_cast<std::byte>(material.alpha_mask_cutoff));
    out.push_back(static_cast<std::byte>(material.diffuse_alpha_mode));
    return out;
}

} // namespace

MaterialId identify(const RenderMaterial& material) {
    const auto canonical = canonical_form(material);
    const auto digest = crypto::sha256_hex(canonical);
    MaterialId id{};
    // The id field on a face is sixteen bytes, so the 32-byte digest is
    // truncated to the first sixteen. That is a collision domain of 2^128 over
    // material definitions, the same bet the blob layer already makes on
    // content addressing.
    for (std::size_t index = 0; index < id.size(); ++index) {
        const auto high = digest[index * 2];
        const auto low = digest[index * 2 + 1];
        const auto nibble = [](char character) -> unsigned {
            if (character >= '0' && character <= '9') return static_cast<unsigned>(character - '0');
            return static_cast<unsigned>(character - 'a' + 10);
        };
        id[index] = static_cast<std::byte>((nibble(high) << 4) | nibble(low));
    }
    return id;
}

std::string format_id(const MaterialId& id) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string text;
    text.reserve(36);
    for (std::size_t index = 0; index < id.size(); ++index) {
        if (index == 4 || index == 6 || index == 8 || index == 10) text.push_back('-');
        const auto byte = std::to_integer<unsigned>(id[index]);
        text.push_back(digits[(byte >> 4) & 0xf]);
        text.push_back(digits[byte & 0xf]);
    }
    return text;
}

ParsedMaterial from_llsd(const llsd::Value& value) {
    ParsedMaterial result;
    if (value.type != llsd::Value::Type::map) return result;
    auto& material = result.material;
    material.normal_map = read_uuid(value, key_normal_map);
    material.normal_offset_x = read_integer(value, key_normal_offset_x, 0);
    material.normal_offset_y = read_integer(value, key_normal_offset_y, 0);
    material.normal_repeat_x = read_integer(value, key_normal_repeat_x, wire_scale);
    material.normal_repeat_y = read_integer(value, key_normal_repeat_y, wire_scale);
    material.normal_rotation = read_integer(value, key_normal_rotation, 0);
    material.specular_map = read_uuid(value, key_specular_map);
    material.specular_offset_x = read_integer(value, key_specular_offset_x, 0);
    material.specular_offset_y = read_integer(value, key_specular_offset_y, 0);
    material.specular_repeat_x = read_integer(value, key_specular_repeat_x, wire_scale);
    material.specular_repeat_y = read_integer(value, key_specular_repeat_y, wire_scale);
    material.specular_rotation = read_integer(value, key_specular_rotation, 0);
    if (const auto* colour = value.find(key_specular_colour);
        colour != nullptr && colour->type == llsd::Value::Type::array) {
        for (std::size_t index = 0; index < material.specular_colour.size() &&
                                    index < colour->elements.size(); ++index)
            material.specular_colour[index] = static_cast<std::uint8_t>(
                std::clamp<std::int64_t>(colour->elements[index].as_integer(255), 0, 255));
    }
    material.specular_exponent = read_byte(value, key_specular_exponent, 0);
    material.environment_intensity = read_byte(value, key_environment_intensity, 0);
    material.alpha_mask_cutoff = read_byte(value, key_alpha_mask_cutoff, 0);
    material.diffuse_alpha_mode = read_byte(value, key_diffuse_alpha_mode, 0);

    // Anything not recognised is reported, not discarded. If the reading of this
    // format is wrong, this is where it shows: a viewer's own keys arrive here
    // and the log names them.
    for (const auto& [key, member] : value.members) {
        static_cast<void>(member);
        if (std::find_if(known_keys.begin(), known_keys.end(), [&key](const char* known) {
                return key == known;
            }) == known_keys.end())
            result.unknown_keys.push_back(key);
    }
    result.ok = true;
    return result;
}

llsd::Value to_llsd(const RenderMaterial& material) {
    llsd::Value out;
    out.type = llsd::Value::Type::map;
    out.members.emplace_back(key_normal_map, uuid_value(material.normal_map));
    out.members.emplace_back(key_normal_offset_x, integer_value(material.normal_offset_x));
    out.members.emplace_back(key_normal_offset_y, integer_value(material.normal_offset_y));
    out.members.emplace_back(key_normal_repeat_x, integer_value(material.normal_repeat_x));
    out.members.emplace_back(key_normal_repeat_y, integer_value(material.normal_repeat_y));
    out.members.emplace_back(key_normal_rotation, integer_value(material.normal_rotation));
    out.members.emplace_back(key_specular_map, uuid_value(material.specular_map));
    out.members.emplace_back(key_specular_offset_x, integer_value(material.specular_offset_x));
    out.members.emplace_back(key_specular_offset_y, integer_value(material.specular_offset_y));
    out.members.emplace_back(key_specular_repeat_x, integer_value(material.specular_repeat_x));
    out.members.emplace_back(key_specular_repeat_y, integer_value(material.specular_repeat_y));
    out.members.emplace_back(key_specular_rotation, integer_value(material.specular_rotation));
    llsd::Value colour;
    colour.type = llsd::Value::Type::array;
    for (const auto component : material.specular_colour)
        colour.elements.push_back(integer_value(component));
    out.members.emplace_back(key_specular_colour, std::move(colour));
    out.members.emplace_back(key_specular_exponent, integer_value(material.specular_exponent));
    out.members.emplace_back(key_environment_intensity,
                             integer_value(material.environment_intensity));
    out.members.emplace_back(key_alpha_mask_cutoff, integer_value(material.alpha_mask_cutoff));
    out.members.emplace_back(key_diffuse_alpha_mode, integer_value(material.diffuse_alpha_mode));
    return out;
}


bool looks_like_material(const llsd::Value& value) {
    if (value.type != llsd::Value::Type::map) return false;
    for (const auto& [key, member] : value.members) {
        static_cast<void>(member);
        if (std::find_if(known_keys.begin(), known_keys.end(), [&key](const char* known) {
                return key == known;
            }) != known_keys.end())
            return true;
    }
    return false;
}

namespace {

void collect_materials(const llsd::Value& value, std::vector<Placement>& out, int depth) {
    if (depth > 16) return;
    if (looks_like_material(value)) {
        // A bare definition with nothing around it to place it.
        out.push_back(Placement{&value, std::nullopt, std::nullopt});
        return;
    }
    if (value.type == llsd::Value::Type::map) {
        // A map holding a definition is that definition's placement: its
        // siblings say which object and face the material is for.
        bool held_definition = false;
        for (const auto& [key, member] : value.members) {
            static_cast<void>(key);
            if (!looks_like_material(member)) continue;
            Placement placement{&member, std::nullopt, std::nullopt};
            if (const auto* id = value.find("ID"); id != nullptr &&
                (id->type == llsd::Value::Type::integer ||
                 id->type == llsd::Value::Type::real))
                placement.local_id = id->as_integer();
            if (const auto* face = value.find("Face"); face != nullptr &&
                (face->type == llsd::Value::Type::integer ||
                 face->type == llsd::Value::Type::real))
                placement.face = face->as_integer();
            out.push_back(placement);
            held_definition = true;
        }
        if (held_definition) return;
        for (const auto& [key, member] : value.members) {
            static_cast<void>(key);
            collect_materials(member, out, depth + 1);
        }
        return;
    }
    if (value.type == llsd::Value::Type::array)
        for (const auto& element : value.elements) collect_materials(element, out, depth + 1);
}

void collect_material_ids(const llsd::Value& value,
                          std::vector<std::array<std::byte, 16>>& out, int depth) {
    if (depth > 16) return;
    if (value.type == llsd::Value::Type::binary && value.binary.size() == 16) {
        std::array<std::byte, 16> id{};
        std::copy(value.binary.begin(), value.binary.end(), id.begin());
        out.push_back(id);
        return;
    }
    if (value.type == llsd::Value::Type::map)
        for (const auto& [key, member] : value.members) {
            static_cast<void>(key);
            collect_material_ids(member, out, depth + 1);
        }
    else if (value.type == llsd::Value::Type::array)
        for (const auto& element : value.elements) collect_material_ids(element, out, depth + 1);
}

void describe_into(const llsd::Value& value, std::string& out, std::size_t limit) {
    if (out.size() >= limit) return;
    switch (value.type) {
    case llsd::Value::Type::undefined: out += "undef"; return;
    case llsd::Value::Type::boolean: out += value.boolean ? "true" : "false"; return;
    case llsd::Value::Type::integer: out += std::to_string(value.integer); return;
    case llsd::Value::Type::real: out += std::to_string(value.real); return;
    case llsd::Value::Type::uuid: out += "uuid(" + value.text + ")"; return;
    case llsd::Value::Type::string: out += "\"" + value.text + "\""; return;
    case llsd::Value::Type::uri: out += "uri(" + value.text + ")"; return;
    case llsd::Value::Type::binary:
        out += "binary[" + std::to_string(value.binary.size()) + "]";
        return;
    case llsd::Value::Type::map:
        out += "{";
        for (std::size_t index = 0; index < value.members.size(); ++index) {
            if (out.size() >= limit) { out += "..."; break; }
            if (index != 0) out += ",";
            out += value.members[index].first + ":";
            describe_into(value.members[index].second, out, limit);
        }
        out += "}";
        return;
    case llsd::Value::Type::array:
        out += "[" + std::to_string(value.elements.size()) + ":";
        for (std::size_t index = 0; index < value.elements.size(); ++index) {
            if (out.size() >= limit) { out += "..."; break; }
            if (index != 0) out += ",";
            describe_into(value.elements[index], out, limit);
        }
        out += "]";
        return;
    }
}

} // namespace

std::vector<Placement> find_materials(const llsd::Value& document) {
    std::vector<Placement> out;
    collect_materials(document, out, 0);
    return out;
}

std::vector<std::array<std::byte, 16>> find_material_ids(const llsd::Value& document) {
    std::vector<std::array<std::byte, 16>> out;
    collect_material_ids(document, out, 0);
    return out;
}

std::string describe(const llsd::Value& value, std::size_t limit) {
    std::string out;
    describe_into(value, out, limit);
    return out;
}

} // namespace homeworldz::material
