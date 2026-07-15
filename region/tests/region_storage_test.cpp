#include "homeworldz/region_storage.h"
#include "homeworldz/sha256.h"

#include <sqlite3.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

int main() {
    const auto suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto path = std::filesystem::temp_directory_path() / ("homeworldz-storage-test-" + suffix);
    try {
        const std::array abc{std::byte{'a'}, std::byte{'b'}, std::byte{'c'}};
        if (homeworldz::crypto::sha256_hex(abc) !=
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") return 1;
        homeworldz::scene::Scene scene;
        const auto first = scene.create("first", {1, 2, 3}, {0.5, 0, 0});
        const auto second = scene.create("second \"line\"\n", {4, 5, 6});
        auto* primitive = scene.find(second);
        if (primitive == nullptr) return 1;
        primitive->object_id = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa";
        primitive->owner_id = "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb";
        primitive->creator_id = "cccccccc-cccc-4ccc-8ccc-cccccccccccc";
        primitive->scale = {0.5, 0.75, 1.25};
        primitive->rotation = {0.25, 0.5, 0.125};
        primitive->description = "storage test primitive";
        primitive->material = 4;
        primitive->physical = true;
        primitive->phantom = true;
        std::filesystem::create_directories(path);
        sqlite3* legacy_database = nullptr;
        if (sqlite3_open((path / "region.db").string().c_str(), &legacy_database) != SQLITE_OK) return 1;
        const auto legacy_result = sqlite3_exec(
            legacy_database,
            "CREATE TABLE asset_mappings (viewer_id TEXT PRIMARY KEY, sha256 TEXT NOT NULL, "
            "size INTEGER NOT NULL, created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP);"
            "INSERT INTO asset_mappings (viewer_id, sha256, size) VALUES ("
            "'99999999-9999-4999-8999-999999999999', "
            "'0000000000000000000000000000000000000000000000000000000000000000', 0);",
            nullptr, nullptr, nullptr);
        sqlite3_close(legacy_database);
        if (legacy_result != SQLITE_OK) return 1;
        {
            homeworldz::storage::RegionStorage storage(path);
            const auto migrated = storage.find_asset("99999999-9999-4999-8999-999999999999");
            if (!migrated || migrated->creator_id != "00000000-0000-0000-0000-000000000000") return 1;
            storage.save_snapshot(scene);
            auto metadata = storage.snapshot_metadata();
            if (metadata.revision != scene.revision() || metadata.path != "scene/snapshot.json") return 1;
            {
                std::ifstream input(path / metadata.path, std::ios::binary);
                const std::string snapshot((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
                if (snapshot.find(R"("name":"first")") == std::string::npos ||
                    snapshot.find(R"("name":"second \"line\"\n")") == std::string::npos) return 1;
            }
            auto* entity = scene.find(first);
            if (entity == nullptr) return 1;
            entity->velocity.x = 1.0;
            entity->avatar_flying = true;
            scene.step(1.0);
            storage.save_snapshot(scene);
            metadata = storage.snapshot_metadata();
            if (metadata.revision != scene.revision() || std::filesystem::exists(path / "scene/snapshot.json.tmp")) return 1;

            homeworldz::scene::Scene restored;
            if (!storage.load_snapshot(restored) || restored.revision() != scene.revision() || restored.size() != 2) return 1;
            const auto* restored_first = restored.find(first);
            const auto* restored_second = restored.find(second);
            if (restored_first == nullptr || restored_first->position.x != 2.0 ||
                restored_first->velocity.x != 1.0 || !restored_first->avatar_flying ||
                restored_second == nullptr ||
                restored_second->name != "second \"line\"\n" ||
                restored_second->object_id != primitive->object_id ||
                restored_second->owner_id != primitive->owner_id ||
                restored_second->creator_id != primitive->creator_id ||
                restored_second->base_permissions != 0x0009e000 ||
                restored_second->next_owner_permissions != 0x0008e000 ||
                restored_second->scale.y != 0.75 || restored_second->rotation.x != 0.25 ||
                restored_second->rotation.y != 0.5 ||
                restored_second->description != "storage test primitive" || restored_second->material != 4 ||
                !restored_second->physical || !restored_second->phantom ||
                restored.create("next") != 3) return 1;

            const std::array content{std::byte{0x00}, std::byte{0x7f}, std::byte{0xff}, std::byte{0x42}};
            const auto first_asset = storage.store_asset(
                "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa", "11111111-1111-4111-8111-111111111111", content);
            const auto second_asset = storage.store_asset(
                "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb", "22222222-2222-4222-8222-222222222222", content);
            if (first_asset.sha256 != second_asset.sha256 || first_asset.size != content.size() ||
                first_asset.creator_id != "11111111-1111-4111-8111-111111111111") return 1;
            const auto mapping = storage.find_asset("aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa");
            if (!mapping || mapping->sha256 != first_asset.sha256 ||
                mapping->creator_id != "11111111-1111-4111-8111-111111111111") return 1;
            const auto migrated_content = std::array{std::byte{0x01}, std::byte{0x02}};
            const auto unknown_asset = storage.store_asset(
                "88888888-8888-4888-8888-888888888888",
                "00000000-0000-0000-0000-000000000000", migrated_content);
            const auto known_asset = storage.store_asset(
                unknown_asset.viewer_id, "33333333-3333-4333-8333-333333333333", migrated_content);
            if (known_asset.creator_id != "33333333-3333-4333-8333-333333333333" ||
                storage.find_asset(unknown_asset.viewer_id)->creator_id != known_asset.creator_id) return 1;
            const auto assets = storage.list_assets();
            const auto contains_asset = [&assets](const homeworldz::storage::AssetMetadata& wanted) {
                return std::any_of(assets.begin(), assets.end(), [&wanted](const auto& asset) {
                    return asset.viewer_id == wanted.viewer_id && asset.creator_id == wanted.creator_id &&
                           asset.sha256 == wanted.sha256 && asset.size == wanted.size;
                });
            };
            if (assets.size() != 4 || !contains_asset(first_asset) || !contains_asset(second_asset) ||
                !contains_asset(known_asset)) return 1;
            bool invalid_creator_rejected = false;
            try {
                storage.store_asset("ffffffff-ffff-4fff-8fff-ffffffffffff", "not-a-uuid", content);
            } catch (const std::invalid_argument&) {
                invalid_creator_rejected = true;
            }
            if (!invalid_creator_rejected) return 1;
            bool conflicting_content_rejected = false;
            try {
                const std::array different{std::byte{0x01}};
                storage.store_asset("aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa",
                                    "11111111-1111-4111-8111-111111111111", different);
            } catch (const std::invalid_argument&) {
                conflicting_content_rejected = true;
            }
            bool conflicting_creator_rejected = false;
            try {
                storage.store_asset("aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa",
                                    "22222222-2222-4222-8222-222222222222", content);
            } catch (const std::invalid_argument&) {
                conflicting_creator_rejected = true;
            }
            const auto repeated = storage.store_asset(
                "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa",
                "11111111-1111-4111-8111-111111111111", content);
            if (!conflicting_content_rejected || !conflicting_creator_rejected ||
                repeated.sha256 != first_asset.sha256 || repeated.creator_id != first_asset.creator_id)
                return 1;
            const auto reconciled = storage.reconcile_asset_creator(
                first_asset.viewer_id, "44444444-4444-4444-8444-444444444444",
                first_asset.sha256, first_asset.size);
            if (reconciled.creator_id != "44444444-4444-4444-8444-444444444444" ||
                storage.find_asset(first_asset.viewer_id)->creator_id != reconciled.creator_id)
                return 1;
            bool invalid_reconciliation_rejected = false;
            try {
                storage.reconcile_asset_creator(first_asset.viewer_id, first_asset.creator_id,
                                                std::string(64, '0'), first_asset.size);
            } catch (const std::invalid_argument&) {
                invalid_reconciliation_rejected = true;
            }
            if (!invalid_reconciliation_rejected) return 1;
            storage.store_baked_texture("eeeeeeee-eeee-4eee-8eee-eeeeeeeeeeee", 8,
                                        "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa");
            if (storage.find_baked_texture("eeeeeeee-eeee-4eee-8eee-eeeeeeeeeeee", 8) !=
                    "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa" ||
                storage.find_baked_texture("eeeeeeee-eeee-4eee-8eee-eeeeeeeeeeee", 9)) return 1;
            const auto loaded = storage.read_asset("bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb");
            if (loaded.size() != content.size() || !std::equal(loaded.begin(), loaded.end(), content.begin())) return 1;
            const auto source = path / "source" / "nested";
            std::filesystem::create_directories(source);
            {
                std::ofstream output(source / "cccccccc-cccc-4ccc-8ccc-cccccccccccc.j2c", std::ios::binary);
                output.write(reinterpret_cast<const char*>(content.data()), static_cast<std::streamsize>(content.size()));
            }
            {
                std::ofstream output(source / "ignored.txt", std::ios::binary);
                output << "ignored";
            }
            {
                std::ofstream output(source / "dddddddd-dddd-4ddd-8ddd-dddddddddddd.bodypart", std::ios::binary);
                output.write(reinterpret_cast<const char*>(content.data()), static_cast<std::streamsize>(content.size()));
            }
            {
                std::ofstream output(source / "eeeeeeee-eeee-4eee-8eee-eeeeeeeeeeee.clothing", std::ios::binary);
                output.write(reinterpret_cast<const char*>(content.data()), static_cast<std::streamsize>(content.size()));
            }
            constexpr std::string_view importer = "33333333-3333-4333-8333-333333333333";
            if (storage.import_asset_directory(path / "missing", importer) != 0 ||
                storage.import_asset_directory(path / "source", importer) != 3 ||
                storage.read_asset("cccccccc-cccc-4ccc-8ccc-cccccccccccc") !=
                    std::vector<std::byte>(content.begin(), content.end()) ||
                storage.read_asset("dddddddd-dddd-4ddd-8ddd-dddddddddddd") !=
                    std::vector<std::byte>(content.begin(), content.end()) ||
                storage.read_asset("eeeeeeee-eeee-4eee-8eee-eeeeeeeeeeee") !=
                    std::vector<std::byte>(content.begin(), content.end()) ||
                storage.find_asset("cccccccc-cccc-4ccc-8ccc-cccccccccccc")->creator_id != importer) return 1;
            const auto blob = path / "assets" / first_asset.sha256.substr(0, 2) / first_asset.sha256.substr(2);
            if (!std::filesystem::is_regular_file(blob)) return 1;
            {
                std::ofstream corrupt(blob, std::ios::binary | std::ios::trunc);
                corrupt << "corrupt";
            }
            bool corruption_detected = false;
            try {
                static_cast<void>(storage.read_asset("aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa"));
            } catch (const std::runtime_error&) {
                corruption_detected = true;
            }
            if (!corruption_detected) return 1;
        }
        std::filesystem::remove_all(path);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        std::filesystem::remove_all(path);
        return 1;
    }
}
