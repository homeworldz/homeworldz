#include "homeworldz/terrain_edit.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <fstream>
#include <memory>
#include <numbers>
#include <set>

namespace homeworldz::terrain {
namespace {

constexpr float minimum_height = 0.0F;
constexpr float maximum_height = 4096.0F;

float neighbor_average(const Heightmap& source, int x, int y) {
    float total{};
    int count{};
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) {
            const auto maximum = static_cast<int>(source.width() - 1);
            const auto sample_x = std::clamp(x + dx, 0, maximum);
            const auto sample_y = std::clamp(y + dy, 0, maximum);
            total += source[static_cast<std::size_t>(sample_y) * source.width() + sample_x];
            ++count;
        }
    return total / static_cast<float>(count);
}

float deterministic_noise(int x, int y) {
    auto value = static_cast<std::uint32_t>(x) * 0x9e3779b9U ^
                 static_cast<std::uint32_t>(y) * 0x85ebca6bU;
    value ^= value >> 16;
    value *= 0x7feb352dU;
    value ^= value >> 15;
    return static_cast<float>(value & 0xffffU) / 32767.5F - 1.0F;
}

} // namespace

std::unique_ptr<Heightmap> load_state(const std::filesystem::path& path,
                                      std::size_t expected_width) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    const auto expected_bytes = expected_width * expected_width * sizeof(float);
    if (!input || input.tellg() != static_cast<std::streamoff>(expected_bytes)) return {};
    input.seekg(0);
    auto result = std::make_unique<Heightmap>(expected_width);
    input.read(reinterpret_cast<char*>(result->data()), static_cast<std::streamsize>(expected_bytes));
    if (!input || std::any_of(result->begin(), result->end(), [](float height) {
            return !std::isfinite(height) || height < minimum_height || height > maximum_height;
        }))
        return {};
    return result;
}

bool save_state(const std::filesystem::path& path, const Heightmap& heightmap) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) return false;
    const auto temporary = path.string() + ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) return false;
        output.write(reinterpret_cast<const char*>(heightmap.data()),
                     static_cast<std::streamsize>(heightmap.size() * sizeof(float)));
        if (!output) return false;
    }
    std::filesystem::remove(path, error);
    error.clear();
    std::filesystem::rename(temporary, path, error);
    return !error;
}

std::vector<viewer::TerrainPatch> apply(Heightmap& heightmap, const Heightmap& revert,
                                        const viewer::ModifyLand& edit) {
    if (edit.action > 5 || edit.areas.empty()) return {};
    if (heightmap.width() != revert.width()) return {};
    const auto original = std::make_unique<Heightmap>(heightmap);
    std::set<std::pair<std::uint8_t, std::uint8_t>> patches;
    for (std::size_t area_index = 0; area_index < edit.areas.size(); ++area_index) {
        const auto& area = edit.areas[area_index];
        const auto center_x = (area.west + area.east) * 0.5F;
        const auto center_y = (area.south + area.north) * 0.5F;
        float radius = static_cast<float>(std::max<std::uint8_t>(1, edit.brush_size));
        if (area_index < edit.extended_brush_sizes.size() && edit.extended_brush_sizes[area_index] > 0.0F)
            radius = edit.extended_brush_sizes[area_index];
        radius = std::clamp(radius, 0.5F, 64.0F);
        const auto maximum = static_cast<int>(heightmap.width() - 1);
        const auto x_from = std::clamp(static_cast<int>(std::floor(center_x - radius)), 0, maximum);
        const auto x_to = std::clamp(static_cast<int>(std::ceil(center_x + radius)), 0, maximum);
        const auto y_from = std::clamp(static_cast<int>(std::floor(center_y - radius)), 0, maximum);
        const auto y_to = std::clamp(static_cast<int>(std::ceil(center_y + radius)), 0, maximum);
        const auto duration = std::clamp(edit.seconds, 0.01F, 4.0F);
        for (int y = y_from; y <= y_to; ++y)
            for (int x = x_from; x <= x_to; ++x) {
                const auto distance = std::hypot(static_cast<float>(x) - center_x,
                                                 static_cast<float>(y) - center_y);
                if (distance > radius) continue;
                const auto weight = std::max(0.0F, std::cos(distance * std::numbers::pi_v<float> /
                                                            (radius * 2.0F)));
                const auto index = static_cast<std::size_t>(y) * heightmap.width() + x;
                float next = heightmap[index];
                switch (edit.action) {
                case 0: next += (edit.height - next) * std::min(1.0F, weight * duration * 0.25F); break;
                case 1: next += weight * duration; break;
                case 2: next -= weight * duration; break;
                case 3: next += (neighbor_average(*original, x, y) - next) *
                                     std::min(1.0F, weight * duration * 0.03F); break;
                case 4: next += deterministic_noise(x, y) * weight * duration * 0.25F; break;
                case 5: next += (revert[index] - next) * std::min(1.0F, weight * duration * 0.25F); break;
                default: break;
                }
                next = std::clamp(next, minimum_height, maximum_height);
                if (std::abs(next - heightmap[index]) < 0.0001F) continue;
                heightmap[index] = next;
                patches.emplace(static_cast<std::uint8_t>(x / 16), static_cast<std::uint8_t>(y / 16));
            }
    }
    std::vector<viewer::TerrainPatch> result;
    result.reserve(patches.size());
    for (const auto [x, y] : patches) result.push_back({x, y});
    return result;
}

} // namespace homeworldz::terrain
