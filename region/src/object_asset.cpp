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
    if (!scale || !rotation || !description || !material ||
        scale->x <= 0.0 || scale->y <= 0.0 || scale->z <= 0.0 ||
        scale->x > 64.0 || scale->y > 64.0 || scale->z > 64.0 ||
        *material < 0.0 || *material > 7.0 || std::floor(*material) != *material)
        return std::nullopt;
    return ObjectAsset{*scale, *rotation, static_cast<std::uint8_t>(*material), *description};
}

} // namespace homeworldz::asset
