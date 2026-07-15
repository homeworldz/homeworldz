#pragma once

#include "homeworldz/scene.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

struct sqlite3;

namespace homeworldz::storage {

struct SnapshotMetadata {
    std::uint64_t revision{};
    std::string path;
};

struct AssetMetadata {
    std::string viewer_id;
    std::string creator_id;
    std::string sha256;
    std::uint64_t size{};
};

class RegionStorage {
public:
    explicit RegionStorage(std::filesystem::path data_path);
    ~RegionStorage();
    RegionStorage(const RegionStorage&) = delete;
    RegionStorage& operator=(const RegionStorage&) = delete;

    void save_snapshot(const scene::Scene& scene);
    bool load_snapshot(scene::Scene& scene) const;
    SnapshotMetadata snapshot_metadata() const;
    AssetMetadata store_asset(std::string viewer_id, std::string creator_id,
                              std::span<const std::byte> content);
    AssetMetadata reconcile_asset_creator(std::string_view viewer_id, std::string_view creator_id,
                                          std::string_view sha256, std::uint64_t size);
    void store_baked_texture(std::string cache_id, std::uint8_t texture_index, std::string asset_id);
    std::optional<std::string> find_baked_texture(std::string_view cache_id,
                                                  std::uint8_t texture_index) const;
    std::size_t import_asset_directory(const std::filesystem::path& directory,
                                       std::string_view creator_id);
    std::vector<AssetMetadata> list_assets() const;
    std::optional<AssetMetadata> find_asset(std::string_view viewer_id) const;
    std::vector<std::byte> read_asset(std::string_view viewer_id) const;

private:
    std::filesystem::path data_path_;
    sqlite3* database_{};
};

} // namespace homeworldz::storage
