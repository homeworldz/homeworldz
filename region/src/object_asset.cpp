#include "homeworldz/object_asset.h"

#include <charconv>
#include <cmath>
#include <string>
#include <string_view>

namespace homeworldz::asset {
namespace {

std::optional<double> number_after(std::string_view content, std::string_view marker,
                                   std::size_t& position) {
    const auto marker_position = content.find(marker, position);
    if (marker_position == std::string_view::npos) return std::nullopt;
    const auto start = marker_position + marker.size();
    double value{};
    const auto parsed = std::from_chars(content.data() + start, content.data() + content.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr == content.data() + start || !std::isfinite(value))
        return std::nullopt;
    position = static_cast<std::size_t>(parsed.ptr - content.data());
    return value;
}

std::optional<scene::Vector3> vector_after(std::string_view content, std::string_view marker) {
    std::size_t position = 0;
    const auto x = number_after(content, marker, position);
    const auto y = number_after(content, ",", position);
    const auto z = number_after(content, ",", position);
    if (!x || !y || !z || position >= content.size() || content[position] != ']') return std::nullopt;
    return scene::Vector3{*x, *y, *z};
}

std::optional<std::string> string_after(std::string_view content, std::string_view marker) {
    const auto marker_position = content.find(marker);
    if (marker_position == std::string_view::npos) return std::nullopt;
    std::string result;
    for (std::size_t position = marker_position + marker.size(); position < content.size(); ++position) {
        const auto character = content[position];
        if (character == '"') return result;
        if (character != '\\') {
            if (static_cast<unsigned char>(character) < 0x20) return std::nullopt;
            result.push_back(character);
            continue;
        }
        if (++position >= content.size()) return std::nullopt;
        switch (content[position]) {
        case '"': result.push_back('"'); break;
        case '\\': result.push_back('\\'); break;
        case '/': result.push_back('/'); break;
        case 'b': result.push_back('\b'); break;
        case 'f': result.push_back('\f'); break;
        case 'n': result.push_back('\n'); break;
        case 'r': result.push_back('\r'); break;
        case 't': result.push_back('\t'); break;
        default: return std::nullopt;
        }
    }
    return std::nullopt;
}

std::optional<std::vector<std::byte>> bytes_from_hex(std::string_view value) {
    if (value.size() % 2 != 0 || value.size() > 131070) return std::nullopt;
    std::vector<std::byte> result(value.size() / 2);
    for (std::size_t index = 0; index < result.size(); ++index) {
        unsigned parsed{};
        const auto converted = std::from_chars(
            value.data() + index * 2, value.data() + index * 2 + 2, parsed, 16);
        if (converted.ec != std::errc{} || converted.ptr != value.data() + index * 2 + 2)
            return std::nullopt;
        result[index] = static_cast<std::byte>(parsed);
    }
    return result;
}

} // namespace

std::optional<ObjectAsset> parse_object_asset(std::span<const std::byte> content) {
    const std::string_view text(reinterpret_cast<const char*>(content.data()), content.size());
    if (text.find(R"("format":"homeworldz-object-v1")") == std::string_view::npos)
        return std::nullopt;
    const auto scale = vector_after(text, R"("scale":[)");
    const auto rotation = vector_after(text, R"("rotation":[)");
    const auto description = string_after(text, R"("description":")");
    std::size_t position = 0;
    const auto material = number_after(text, R"("material":)", position);
    position = 0;
    auto physics_shape_type = number_after(text, R"("physicsShapeType":)", position);
    position = 0;
    auto physics_density = number_after(text, R"("physicsDensity":)", position);
    position = 0;
    auto physics_friction = number_after(text, R"("physicsFriction":)", position);
    position = 0;
    auto physics_restitution = number_after(text, R"("physicsRestitution":)", position);
    position = 0;
    auto physics_gravity_multiplier = number_after(text, R"("physicsGravityMultiplier":)", position);
    position = 0;
    auto path_curve = number_after(text, R"("pathCurve":)", position);
    position = 0;
    auto profile_curve = number_after(text, R"("profileCurve":)", position);
    const auto texture_entry_hex = string_after(text, R"("textureEntry":")");
    if (!scale || !rotation || !description || !material ||
        scale->x <= 0.0 || scale->y <= 0.0 || scale->z <= 0.0 ||
        scale->x > 64.0 || scale->y > 64.0 || scale->z > 64.0 ||
        *material < 0.0 || *material > 7.0 || std::floor(*material) != *material)
        return std::nullopt;
    if (!physics_shape_type) physics_shape_type = 0.0;
    if (!physics_density) physics_density = 1000.0;
    if (!physics_friction) physics_friction = 0.6;
    if (!physics_restitution) physics_restitution = 0.5;
    if (!physics_gravity_multiplier) physics_gravity_multiplier = 1.0;
    if (!path_curve) path_curve = 0x10;
    if (!profile_curve) profile_curve = 0x01;
    if (*physics_shape_type < 0.0 || *physics_shape_type > 2.0 ||
        std::floor(*physics_shape_type) != *physics_shape_type ||
        *physics_density < 1.0 || *physics_density > 22587.0 ||
        *physics_friction < 0.0 || *physics_friction > 255.0 ||
        *physics_restitution < 0.0 || *physics_restitution > 1.0 ||
        *physics_gravity_multiplier < -1.0 || *physics_gravity_multiplier > 28.0 ||
        *path_curve < 0.0 || *path_curve > 255.0 || std::floor(*path_curve) != *path_curve ||
        *profile_curve < 0.0 || *profile_curve > 255.0 ||
        std::floor(*profile_curve) != *profile_curve)
        return std::nullopt;
    auto texture_entry = texture_entry_hex
        ? bytes_from_hex(*texture_entry_hex) : std::optional<std::vector<std::byte>>{{}};
    if (!texture_entry) return std::nullopt;
    return ObjectAsset{*scale, *rotation, static_cast<std::uint8_t>(*material), *description,
        static_cast<std::uint8_t>(*physics_shape_type), *physics_density, *physics_friction,
        *physics_restitution, *physics_gravity_multiplier, std::move(*texture_entry),
        static_cast<std::uint8_t>(*path_curve), static_cast<std::uint8_t>(*profile_curve)};
}

} // namespace homeworldz::asset
