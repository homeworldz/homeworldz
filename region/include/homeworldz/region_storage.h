#pragma once

#include "homeworldz/scene.h"

#include <cstdint>
#include <filesystem>
#include <string>

struct sqlite3;

namespace homeworldz::storage {

struct SnapshotMetadata {
    std::uint64_t revision{};
    std::string path;
};

class RegionStorage {
public:
    explicit RegionStorage(std::filesystem::path data_path);
    ~RegionStorage();
    RegionStorage(const RegionStorage&) = delete;
    RegionStorage& operator=(const RegionStorage&) = delete;

    void save_snapshot(const scene::Scene& scene);
    SnapshotMetadata snapshot_metadata() const;

private:
    std::filesystem::path data_path_;
    sqlite3* database_{};
};

} // namespace homeworldz::storage
