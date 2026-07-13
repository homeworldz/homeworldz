#include "homeworldz/region_storage.h"

#include "homeworldz/api_models.h"
#include "homeworldz/sha256.h"

#include <sqlite3.h>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <utility>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace homeworldz::storage {
namespace {

void execute(sqlite3* database, const char* sql) {
    char* message = nullptr;
    if (sqlite3_exec(database, sql, nullptr, nullptr, &message) != SQLITE_OK) {
        const std::string error = message == nullptr ? "SQLite command failed" : message;
        sqlite3_free(message);
        throw std::runtime_error(error);
    }
}

void replace_file(const std::filesystem::path& source, const std::filesystem::path& target) {
#ifdef _WIN32
    if (!MoveFileExW(source.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        throw std::runtime_error("replace scene snapshot failed with Windows error " + std::to_string(GetLastError()));
    }
#else
    if (std::rename(source.c_str(), target.c_str()) != 0) {
        throw std::runtime_error("replace scene snapshot failed");
    }
#endif
}

std::string snapshot_json(const scene::Scene& scene) {
    std::vector<const scene::Entity*> entities;
    entities.reserve(scene.entities().size());
    for (const auto& [id, entity] : scene.entities()) {
        static_cast<void>(id);
        entities.push_back(&entity);
    }
    std::sort(entities.begin(), entities.end(), [](const auto* left, const auto* right) {
        return left->id < right->id;
    });
    std::string json = "{\"revision\":" + std::to_string(scene.revision()) + ",\"entities\":[";
    bool first = true;
    for (const auto* entity : entities) {
        if (!first) json += ',';
        first = false;
        json += "{\"id\":" + std::to_string(entity->id) + ",\"name\":" + api::json_string(entity->name) +
                ",\"position\":[" + std::to_string(entity->position.x) + ',' +
                std::to_string(entity->position.y) + ',' + std::to_string(entity->position.z) +
                "],\"velocity\":[" + std::to_string(entity->velocity.x) + ',' +
                std::to_string(entity->velocity.y) + ',' + std::to_string(entity->velocity.z) + "]}";
    }
    return json + "]}";
}

} // namespace

RegionStorage::RegionStorage(std::filesystem::path data_path) : data_path_(std::move(data_path)) {
    std::filesystem::create_directories(data_path_ / "scene");
    std::filesystem::create_directories(data_path_ / "assets");
    std::filesystem::create_directories(data_path_ / "logs");
    const auto database_path = data_path_ / "region.db";
    if (sqlite3_open(database_path.string().c_str(), &database_) != SQLITE_OK) {
        const std::string error = database_ == nullptr ? "open region database failed" : sqlite3_errmsg(database_);
        if (database_ != nullptr) sqlite3_close(database_);
        database_ = nullptr;
        throw std::runtime_error(error);
    }
    execute(database_, "PRAGMA journal_mode=WAL; PRAGMA foreign_keys=ON;"
                       "CREATE TABLE IF NOT EXISTS scene_metadata ("
                       "id INTEGER PRIMARY KEY CHECK (id = 1), revision INTEGER NOT NULL,"
                       "snapshot_path TEXT NOT NULL, saved_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP);"
                       "INSERT OR IGNORE INTO scene_metadata (id, revision, snapshot_path) VALUES (1, 0, 'scene/snapshot.json');"
                       "CREATE TABLE IF NOT EXISTS asset_mappings ("
                       "viewer_id TEXT PRIMARY KEY, sha256 TEXT NOT NULL, size INTEGER NOT NULL,"
                       "created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP);"
                       "CREATE INDEX IF NOT EXISTS asset_mappings_sha256 ON asset_mappings(sha256);");
}

RegionStorage::~RegionStorage() {
    if (database_ != nullptr) sqlite3_close(database_);
}

void RegionStorage::save_snapshot(const scene::Scene& scene) {
    const auto relative_path = std::filesystem::path("scene") / "snapshot.json";
    const auto target = data_path_ / relative_path;
    const auto temporary = target.string() + ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        output.exceptions(std::ios::failbit | std::ios::badbit);
        output << snapshot_json(scene);
        output.flush();
    }
    replace_file(temporary, target);
    sqlite3_stmt* statement = nullptr;
    const char* sql = "UPDATE scene_metadata SET revision = ?, snapshot_path = ?, saved_at = CURRENT_TIMESTAMP WHERE id = 1";
    if (sqlite3_prepare_v2(database_, sql, -1, &statement, nullptr) != SQLITE_OK) {
        throw std::runtime_error(sqlite3_errmsg(database_));
    }
    sqlite3_bind_int64(statement, 1, static_cast<sqlite3_int64>(scene.revision()));
    const auto path = relative_path.generic_string();
    sqlite3_bind_text(statement, 2, path.c_str(), -1, SQLITE_TRANSIENT);
    const auto result = sqlite3_step(statement);
    sqlite3_finalize(statement);
    if (result != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_));
}

SnapshotMetadata RegionStorage::snapshot_metadata() const {
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(database_, "SELECT revision, snapshot_path FROM scene_metadata WHERE id = 1", -1,
                           &statement, nullptr) != SQLITE_OK) {
        throw std::runtime_error(sqlite3_errmsg(database_));
    }
    SnapshotMetadata metadata;
    if (sqlite3_step(statement) == SQLITE_ROW) {
        metadata.revision = static_cast<std::uint64_t>(sqlite3_column_int64(statement, 0));
        metadata.path = reinterpret_cast<const char*>(sqlite3_column_text(statement, 1));
    }
    sqlite3_finalize(statement);
    return metadata;
}

AssetMetadata RegionStorage::store_asset(std::string viewer_id, std::span<const std::byte> content) {
    AssetMetadata metadata{std::move(viewer_id), crypto::sha256_hex(content), content.size()};
    const auto relative = std::filesystem::path("assets") / metadata.sha256.substr(0, 2) / metadata.sha256.substr(2);
    const auto target = data_path_ / relative;
    if (!std::filesystem::exists(target)) {
        std::filesystem::create_directories(target.parent_path());
        const auto temporary = target.string() + ".tmp";
        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            output.exceptions(std::ios::failbit | std::ios::badbit);
            output.write(reinterpret_cast<const char*>(content.data()), static_cast<std::streamsize>(content.size()));
            output.flush();
        }
        replace_file(temporary, target);
    }
    sqlite3_stmt* statement = nullptr;
    const char* sql = "INSERT INTO asset_mappings (viewer_id, sha256, size) VALUES (?, ?, ?) "
                      "ON CONFLICT(viewer_id) DO UPDATE SET sha256=excluded.sha256, size=excluded.size";
    if (sqlite3_prepare_v2(database_, sql, -1, &statement, nullptr) != SQLITE_OK) {
        throw std::runtime_error(sqlite3_errmsg(database_));
    }
    sqlite3_bind_text(statement, 1, metadata.viewer_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 2, metadata.sha256.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(statement, 3, static_cast<sqlite3_int64>(metadata.size));
    const auto result = sqlite3_step(statement);
    sqlite3_finalize(statement);
    if (result != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_));
    return metadata;
}

std::optional<AssetMetadata> RegionStorage::find_asset(std::string_view viewer_id) const {
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(database_, "SELECT viewer_id, sha256, size FROM asset_mappings WHERE viewer_id = ?", -1,
                           &statement, nullptr) != SQLITE_OK) {
        throw std::runtime_error(sqlite3_errmsg(database_));
    }
    sqlite3_bind_text(statement, 1, viewer_id.data(), static_cast<int>(viewer_id.size()), SQLITE_TRANSIENT);
    std::optional<AssetMetadata> metadata;
    if (sqlite3_step(statement) == SQLITE_ROW) {
        metadata = AssetMetadata{
            reinterpret_cast<const char*>(sqlite3_column_text(statement, 0)),
            reinterpret_cast<const char*>(sqlite3_column_text(statement, 1)),
            static_cast<std::uint64_t>(sqlite3_column_int64(statement, 2))};
    }
    sqlite3_finalize(statement);
    return metadata;
}

std::vector<std::byte> RegionStorage::read_asset(std::string_view viewer_id) const {
    const auto metadata = find_asset(viewer_id);
    if (!metadata) throw std::runtime_error("asset mapping was not found");
    const auto path = data_path_ / "assets" / metadata->sha256.substr(0, 2) / metadata->sha256.substr(2);
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) throw std::runtime_error("asset blob was not found");
    const auto length = static_cast<std::streamsize>(input.tellg());
    input.seekg(0);
    std::vector<std::byte> content(static_cast<std::size_t>(length));
    input.read(reinterpret_cast<char*>(content.data()), length);
    if (crypto::sha256_hex(content) != metadata->sha256) {
        throw std::runtime_error("asset blob failed content hash verification");
    }
    return content;
}

} // namespace homeworldz::storage
