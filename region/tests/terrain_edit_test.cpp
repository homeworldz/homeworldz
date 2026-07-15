#include "homeworldz/terrain_edit.h"

#include <cmath>
#include <filesystem>
#include <memory>

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
    return 0;
}
