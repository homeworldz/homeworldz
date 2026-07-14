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
        primitive->material = 4;
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
            scene.step(1.0);
            storage.save_snapshot(scene);
            metadata = storage.snapshot_metadata();
            if (metadata.revision != scene.revision() || std::filesystem::exists(path / "scene/snapshot.json.tmp")) return 1;

            homeworldz::scene::Scene restored;
            if (!storage.load_snapshot(restored) || restored.revision() != scene.revision() || restored.size() != 2) return 1;
            const auto* restored_first = restored.find(first);
            const auto* restored_second = restored.find(second);
            if (restored_first == nullptr || restored_first->position.x != 2.0 ||
                restored_first->velocity.x != 1.0 || restored_second == nullptr ||
                restored_second->name != "second \"line\"\n" ||
                restored_second->object_id != primitive->object_id ||
                restored_second->owner_id != primitive->owner_id ||
                restored_second->creator_id != primitive->creator_id ||
                restored_second->base_permissions != 0x0009e000 ||
                restored_second->next_owner_permissions != 0x0008e000 ||
                restored_second->scale.y != 0.75 || restored_second->material != 4 ||
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
            bool invalid_creator_rejected = false;
            try {
                storage.store_asset("ffffffff-ffff-4fff-8fff-ffffffffffff", "not-a-uuid", content);
            } catch (const std::invalid_argument&) {
                invalid_creator_rejected = true;
            }
            if (!invalid_creator_rejected) return 1;
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
