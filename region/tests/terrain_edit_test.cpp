#include "homeworldz/terrain_edit.h"

#include <cmath>
#include <filesystem>
#include <memory>
#include <stdexcept>

int main() {
    auto terrain = std::make_unique<homeworldz::terrain::Heightmap>();
    terrain->fill(20.0F);
    auto revert = std::make_unique<homeworldz::terrain::Heightmap>(*terrain);
    homeworldz::viewer::ModifyLand raise;
    raise.action = 1;
    raise.seconds = 1.0F;
    raise.brush_size = 1;
    raise.areas.push_back({-1, 128.0F, 128.0F, 128.0F, 128.0F});
    raise.extended_brush_sizes.push_back(4.0F);
    const auto changed = homeworldz::terrain::apply(*terrain, *revert, raise);
    if (changed.empty() || (*terrain)[128 * 256 + 128] <= 20.9F ||
        (*terrain)[100 * 256 + 100] != 20.0F)
        return 1;

    auto level = raise;
    level.action = 0;
    level.height = 22.0F;
    level.seconds = 4.0F;
    if (homeworldz::terrain::apply(*terrain, *revert, level).empty() ||
        std::abs((*terrain)[128 * 256 + 128] - 22.0F) > 0.001F)
        return 2;

    auto restore = raise;
    restore.action = 5;
    restore.seconds = 4.0F;
    if (homeworldz::terrain::apply(*terrain, *revert, restore).empty() ||
        std::abs((*terrain)[128 * 256 + 128] - 20.0F) > 0.001F)
        return 3;

    const auto path = std::filesystem::temp_directory_path() / "homeworldz-terrain-edit-test.f32";
    if (!homeworldz::terrain::save_state(path, *terrain)) return 4;
    const auto loaded = homeworldz::terrain::load_state(path);
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    if (!loaded || *loaded != *terrain) return 5;

    homeworldz::terrain::Heightmap large(512);
    large.fill(20.0F);
    const auto large_revert = large;
    auto large_raise = raise;
    large_raise.areas.clear();
    large_raise.areas.push_back({-1, 400.0F, 300.0F, 400.0F, 300.0F});
    if (homeworldz::terrain::apply(large, large_revert, large_raise).empty() ||
        large[300 * 512 + 400] <= 20.9F || large[128 * 512 + 128] != 20.0F)
        return 6;

    const auto large_path = std::filesystem::temp_directory_path() /
        "homeworldz-terrain-edit-large-test.f32";
    if (!homeworldz::terrain::save_state(large_path, large)) return 7;
    const auto loaded_large = homeworldz::terrain::load_state(large_path, 512);
    const auto wrong_size = homeworldz::terrain::load_state(large_path, 256);
    std::filesystem::remove(large_path, ignored);
    if (!loaded_large || *loaded_large != large || wrong_size) return 8;

    homeworldz::terrain::Heightmap maximum(1024);
    if (maximum.width() != 1024 || maximum.size() != 1024 * 1024) return 9;
    if (!homeworldz::terrain::apply(maximum, large_revert, large_raise).empty()) return 10;
    try {
        homeworldz::terrain::Heightmap unsupported(768);
        return 11;
    } catch (const std::invalid_argument&) {
    }
    return 0;
}
