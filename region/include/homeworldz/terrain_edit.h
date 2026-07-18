#pragma once

#include "homeworldz/viewer_protocol.h"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <span>
#include <stdexcept>
#include <vector>

namespace homeworldz::terrain {

class Heightmap {
public:
    explicit Heightmap(std::size_t width = 256) : width_(width), samples_(width * width) {
        if (width != 256 && width != 512 && width != 1024)
            throw std::invalid_argument("terrain width must be 256, 512, or 1024");
    }

    std::size_t width() const noexcept { return width_; }
    std::size_t size() const noexcept { return samples_.size(); }
    float* data() noexcept { return samples_.data(); }
    const float* data() const noexcept { return samples_.data(); }
    auto begin() noexcept { return samples_.begin(); }
    auto end() noexcept { return samples_.end(); }
    auto begin() const noexcept { return samples_.begin(); }
    auto end() const noexcept { return samples_.end(); }
    float& operator[](std::size_t index) noexcept { return samples_[index]; }
    const float& operator[](std::size_t index) const noexcept { return samples_[index]; }
    void fill(float value) { std::fill(samples_.begin(), samples_.end(), value); }
    operator std::span<const float>() const noexcept {
        return {samples_.data(), samples_.size()};
    }
    bool operator==(const Heightmap&) const = default;

private:
    std::size_t width_;
    std::vector<float> samples_;
};

std::unique_ptr<Heightmap> load_state(const std::filesystem::path& path,
                                      std::size_t expected_width = 256);
bool save_state(const std::filesystem::path& path, const Heightmap& heightmap);
std::vector<viewer::TerrainPatch> apply(Heightmap& heightmap, const Heightmap& revert,
                                        const viewer::ModifyLand& edit);

} // namespace homeworldz::terrain
