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
    AssetMetadata store_asset(std::string viewer_id, std::span<const std::byte> content);
    std::size_t import_asset_directory(const std::filesystem::path& directory);
    std::optional<AssetMetadata> find_asset(std::string_view viewer_id) const;
    std::vector<std::byte> read_asset(std::string_view viewer_id) const;

private:
    std::filesystem::path data_path_;
    sqlite3* database_{};
};

} // namespace homeworldz::storage
