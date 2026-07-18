#include "homeworldz/region_storage.h"

#include "homeworldz/api_models.h"
#include "homeworldz/physics_scene.h"
#include "homeworldz/sha256.h"

#include <sqlite3.h>

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string_view>
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

bool has_column(sqlite3* database, const char* table, std::string_view column) {
    sqlite3_stmt* statement = nullptr;
    const auto sql = std::string("PRAGMA table_info(") + table + ')';
    if (sqlite3_prepare_v2(database, sql.c_str(), -1, &statement, nullptr) != SQLITE_OK)
        throw std::runtime_error(sqlite3_errmsg(database));
    bool found = false;
    while (sqlite3_step(statement) == SQLITE_ROW) {
        const auto* name = reinterpret_cast<const char*>(sqlite3_column_text(statement, 1));
        if (name != nullptr && column == name) {
            found = true;
            break;
        }
    }
    sqlite3_finalize(statement);
    return found;
}

bool valid_uuid(std::string_view value) {
    if (value.size() != 36 || value[8] != '-' || value[13] != '-' ||
        value[18] != '-' || value[23] != '-') return false;
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (index == 8 || index == 13 || index == 18 || index == 23) continue;
        if (!std::isxdigit(static_cast<unsigned char>(value[index]))) return false;
    }
    return true;
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

void apply_snapshot_material_defaults(scene::Entity& entity) {
    const auto material = physics::material_properties(entity.material);
    entity.physics_density = material.density;
    entity.physics_friction = material.friction;
    entity.physics_restitution = material.restitution;
}

std::string bytes_to_hex(std::span<const std::byte> bytes) {
    constexpr char hex[] = "0123456789abcdef";
    std::string result(bytes.size() * 2, '0');
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        const auto value = std::to_integer<unsigned>(bytes[index]);
        result[index * 2] = hex[value >> 4];
        result[index * 2 + 1] = hex[value & 0x0f];
    }
    return result;
}

std::vector<std::byte> bytes_from_hex(std::string_view value) {
    if (value.size() % 2 != 0 || value.size() > 131070)
        throw std::runtime_error("invalid texture entry encoding");
    std::vector<std::byte> result(value.size() / 2);
    for (std::size_t index = 0; index < result.size(); ++index) {
        unsigned parsed{};
        const auto converted = std::from_chars(
            value.data() + index * 2, value.data() + index * 2 + 2, parsed, 16);
        if (converted.ec != std::errc{} || converted.ptr != value.data() + index * 2 + 2)
            throw std::runtime_error("invalid texture entry encoding");
        result[index] = static_cast<std::byte>(parsed);
    }
    return result;
}

class SnapshotReader {
public:
    explicit SnapshotReader(std::string_view input) : input_(input) {}

    std::pair<std::uint64_t, std::vector<scene::Entity>> read() {
        expect("{");
        expect_string("revision");
        expect(":");
        const auto revision = unsigned_integer();
        expect(",");
        expect_string("entities");
        expect(":");
        expect("[");
        std::vector<scene::Entity> entities;
        if (!consume("]")) {
            do entities.push_back(entity()); while (consume(","));
            expect("]");
        }
        expect("}");
        skip_space();
        if (position_ != input_.size()) fail("unexpected content after snapshot");
        return {revision, std::move(entities)};
    }

private:
    [[noreturn]] void fail(const char* message) const {
        throw std::runtime_error(std::string("parse scene snapshot at byte ") +
                                 std::to_string(position_) + ": " + message);
    }

    void skip_space() {
        while (position_ < input_.size() &&
               std::isspace(static_cast<unsigned char>(input_[position_]))) ++position_;
    }

    bool consume(std::string_view token) {
        skip_space();
        if (!input_.substr(position_).starts_with(token)) return false;
        position_ += token.size();
        return true;
    }

    void expect(std::string_view token) {
        if (!consume(token)) fail("unexpected token");
    }

    void expect_string(std::string_view expected) {
        if (string() != expected) fail("unexpected object field");
    }

    std::uint64_t unsigned_integer() {
        skip_space();
        std::uint64_t value{};
        const auto* first = input_.data() + position_;
        const auto* last = input_.data() + input_.size();
        const auto result = std::from_chars(first, last, value);
        if (result.ec != std::errc{} || result.ptr == first) fail("expected unsigned integer");
        position_ = static_cast<std::size_t>(result.ptr - input_.data());
        return value;
    }

    double number() {
        skip_space();
        double value{};
        const auto* first = input_.data() + position_;
        const auto* last = input_.data() + input_.size();
        const auto result = std::from_chars(first, last, value);
        if (result.ec != std::errc{} || result.ptr == first) fail("expected number");
        position_ = static_cast<std::size_t>(result.ptr - input_.data());
        return value;
    }

    bool boolean() {
        if (consume("true")) return true;
        if (consume("false")) return false;
        fail("expected boolean");
    }

    static int hex_digit(char value) {
        if (value >= '0' && value <= '9') return value - '0';
        if (value >= 'a' && value <= 'f') return value - 'a' + 10;
        if (value >= 'A' && value <= 'F') return value - 'A' + 10;
        return -1;
    }

    std::string string() {
        skip_space();
        if (position_ >= input_.size() || input_[position_++] != '"') fail("expected string");
        std::string result;
        while (position_ < input_.size()) {
            const char value = input_[position_++];
            if (value == '"') return result;
            if (static_cast<unsigned char>(value) < 0x20) fail("unescaped control character");
            if (value != '\\') {
                result.push_back(value);
                continue;
            }
            if (position_ >= input_.size()) fail("incomplete string escape");
            switch (input_[position_++]) {
            case '"': result.push_back('"'); break;
            case '\\': result.push_back('\\'); break;
            case '/': result.push_back('/'); break;
            case 'b': result.push_back('\b'); break;
            case 'f': result.push_back('\f'); break;
            case 'n': result.push_back('\n'); break;
            case 'r': result.push_back('\r'); break;
            case 't': result.push_back('\t'); break;
            case 'u': {
                if (position_ + 4 > input_.size()) fail("incomplete unicode escape");
                unsigned codepoint{};
                for (int i = 0; i < 4; ++i) {
                    const int digit = hex_digit(input_[position_++]);
                    if (digit < 0) fail("invalid unicode escape");
                    codepoint = codepoint * 16 + static_cast<unsigned>(digit);
                }
                if (codepoint >= 0xd800 && codepoint <= 0xdfff) fail("unsupported unicode surrogate");
                if (codepoint <= 0x7f) {
                    result.push_back(static_cast<char>(codepoint));
                } else if (codepoint <= 0x7ff) {
                    result.push_back(static_cast<char>(0xc0 | (codepoint >> 6)));
                    result.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
                } else {
                    result.push_back(static_cast<char>(0xe0 | (codepoint >> 12)));
                    result.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
                    result.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
                }
                break;
            }
            default: fail("invalid string escape");
            }
        }
        fail("unterminated string");
    }

    scene::Vector3 vector() {
        expect("[");
        const auto x = number();
        expect(",");
        const auto y = number();
        expect(",");
        const auto z = number();
        expect("]");
        return {x, y, z};
    }

    scene::TaskInventoryItem task_inventory_item() {
        scene::TaskInventoryItem item;
        expect("{");
        expect_string("itemId"); expect(":"); item.item_id = string(); expect(",");
        expect_string("assetId"); expect(":"); item.asset_id = string(); expect(",");
        expect_string("creatorId"); expect(":"); item.creator_id = string(); expect(",");
        expect_string("ownerId"); expect(":"); item.owner_id = string(); expect(",");
        expect_string("lastOwnerId"); expect(":"); item.last_owner_id = string(); expect(",");
        expect_string("groupId"); expect(":"); item.group_id = string(); expect(",");
        expect_string("name"); expect(":"); item.name = string(); expect(",");
        expect_string("description"); expect(":"); item.description = string(); expect(",");
        expect_string("assetType"); expect(":");
        const auto asset_type = unsigned_integer();
        if (asset_type > 127) fail("task inventory asset type is outside the supported range");
        item.asset_type = static_cast<std::int8_t>(asset_type); expect(",");
        expect_string("inventoryType"); expect(":");
        const auto inventory_type = unsigned_integer();
        if (inventory_type > 127) fail("task inventory type is outside the supported range");
        item.inventory_type = static_cast<std::int8_t>(inventory_type); expect(",");
        expect_string("flags"); expect(":");
        item.flags = static_cast<std::uint32_t>(unsigned_integer()); expect(",");
        expect_string("basePermissions"); expect(":");
        item.base_permissions = static_cast<std::uint32_t>(unsigned_integer()); expect(",");
        expect_string("currentPermissions"); expect(":");
        item.current_permissions = static_cast<std::uint32_t>(unsigned_integer()); expect(",");
        expect_string("groupPermissions"); expect(":");
        item.group_permissions = static_cast<std::uint32_t>(unsigned_integer()); expect(",");
        expect_string("everyonePermissions"); expect(":");
        item.everyone_permissions = static_cast<std::uint32_t>(unsigned_integer()); expect(",");
        expect_string("nextPermissions"); expect(":");
        item.next_permissions = static_cast<std::uint32_t>(unsigned_integer()); expect(",");
        expect_string("saleType"); expect(":");
        const auto sale_type = unsigned_integer();
        if (sale_type > 255) fail("task inventory sale type is outside the supported range");
        item.sale_type = static_cast<std::uint8_t>(sale_type); expect(",");
        expect_string("salePrice"); expect(":");
        const auto sale_price = unsigned_integer();
        if (sale_price > static_cast<std::uint64_t>((std::numeric_limits<std::int32_t>::max)()))
            fail("task inventory sale price is outside the supported range");
        item.sale_price = static_cast<std::int32_t>(sale_price); expect(",");
        expect_string("creationDate"); expect(":"); item.creation_date = unsigned_integer();
        expect("}");
        return item;
    }

    scene::Entity entity() {
        expect("{");
        expect_string("id");
        expect(":");
        const auto id = unsigned_integer();
        expect(",");
        expect_string("name");
        expect(":");
        auto name = string();
        expect(",");
        expect_string("position");
        expect(":");
        const auto position = vector();
        expect(",");
        expect_string("velocity");
        expect(":");
        const auto velocity = vector();
        if (consume("}")) return {id, std::move(name), position, velocity};
        expect(",");
        expect_string("objectId");
        expect(":");
        auto object_id = string();
        expect(",");
        expect_string("ownerId");
        expect(":");
        auto owner_id = string();
        expect(",");
        expect_string("scale");
        expect(":");
        const auto scale = vector();
        expect(",");
        expect_string("material");
        expect(":");
        const auto material = unsigned_integer();
        if (material > 255) fail("material is outside the supported range");
        scene::Entity result{id, std::move(name), position, velocity, std::move(object_id),
                             std::move(owner_id), scale, static_cast<std::uint8_t>(material)};
        apply_snapshot_material_defaults(result);
        if (consume("}")) {
            result.creator_id = result.owner_id;
            return result;
        }
        expect(",");
        expect_string("creatorId");
        expect(":");
        result.creator_id = string();
        expect(",");
        expect_string("basePermissions");
        expect(":");
        result.base_permissions = static_cast<std::uint32_t>(unsigned_integer());
        expect(",");
        expect_string("ownerPermissions");
        expect(":");
        result.owner_permissions = static_cast<std::uint32_t>(unsigned_integer());
        expect(",");
        expect_string("groupPermissions");
        expect(":");
        result.group_permissions = static_cast<std::uint32_t>(unsigned_integer());
        expect(",");
        expect_string("everyonePermissions");
        expect(":");
        result.everyone_permissions = static_cast<std::uint32_t>(unsigned_integer());
        expect(",");
        expect_string("nextOwnerPermissions");
        expect(":");
        result.next_owner_permissions = static_cast<std::uint32_t>(unsigned_integer());
        expect(",");
        expect_string("creationDate");
        expect(":");
        result.creation_date = unsigned_integer();
        if (consume("}")) return result;
        expect(",");
        expect_string("rotation");
        expect(":");
        result.rotation = vector();
        if (consume("}")) return result;
        expect(",");
        expect_string("description");
        expect(":");
        result.description = string();
        if (consume("}")) return result;
        expect(",");
        expect_string("avatarFlying");
        expect(":");
        result.avatar_flying = boolean();
        if (consume("}")) return result;
        expect(",");
        expect_string("physical");
        expect(":");
        result.physical = boolean();
        if (consume("}")) return result;
        expect(",");
        expect_string("phantom");
        expect(":");
        result.phantom = boolean();
        if (consume("}")) return result;
        expect(",");
        expect_string("physicsShapeType");
        expect(":");
        const auto physics_shape_type = unsigned_integer();
        if (physics_shape_type > 255) fail("physics shape type is outside the supported range");
        result.physics_shape_type = static_cast<std::uint8_t>(physics_shape_type);
        expect(",");
        expect_string("physicsDensity");
        expect(":");
        result.physics_density = number();
        expect(",");
        expect_string("physicsFriction");
        expect(":");
        result.physics_friction = number();
        expect(",");
        expect_string("physicsRestitution");
        expect(":");
        result.physics_restitution = number();
        expect(",");
        expect_string("physicsGravityMultiplier");
        expect(":");
        result.physics_gravity_multiplier = number();
        if (consume("}")) return result;
        expect(",");
        expect_string("textureEntry");
        expect(":");
        result.texture_entry = bytes_from_hex(string());
        if (consume("}")) return result;
        expect(",");
        expect_string("pathCurve");
        expect(":");
        const auto path_curve = unsigned_integer();
        if (path_curve > 255) fail("path curve is outside the supported range");
        result.path_curve = static_cast<std::uint8_t>(path_curve);
        expect(",");
        expect_string("profileCurve");
        expect(":");
        const auto profile_curve = unsigned_integer();
        if (profile_curve > 255) fail("profile curve is outside the supported range");
        result.profile_curve = static_cast<std::uint8_t>(profile_curve);
        if (consume("}")) return result;
        const auto read_byte_field = [&](std::string_view name) {
            expect(",");
            expect_string(name);
            expect(":");
            const auto value = unsigned_integer();
            if (value > 255) fail("primitive shape byte is outside the supported range");
            return static_cast<std::uint8_t>(value);
        };
        const auto read_word_field = [&](std::string_view name) {
            expect(",");
            expect_string(name);
            expect(":");
            const auto value = unsigned_integer();
            if (value > 65535) fail("primitive shape word is outside the supported range");
            return static_cast<std::uint16_t>(value);
        };
        result.path_begin = read_word_field("pathBegin");
        result.path_end = read_word_field("pathEnd");
        result.path_scale_x = read_byte_field("pathScaleX");
        result.path_scale_y = read_byte_field("pathScaleY");
        result.path_shear_x = read_byte_field("pathShearX");
        result.path_shear_y = read_byte_field("pathShearY");
        result.path_twist = read_byte_field("pathTwist");
        result.path_twist_begin = read_byte_field("pathTwistBegin");
        result.path_radius_offset = read_byte_field("pathRadiusOffset");
        result.path_taper_x = read_byte_field("pathTaperX");
        result.path_taper_y = read_byte_field("pathTaperY");
        result.path_revolutions = read_byte_field("pathRevolutions");
        result.path_skew = read_byte_field("pathSkew");
        result.profile_begin = read_word_field("profileBegin");
        result.profile_end = read_word_field("profileEnd");
        result.profile_hollow = read_word_field("profileHollow");
        if (consume("}")) return result;
        expect(",");
        expect_string("parentId");
        expect(":");
        result.parent_id = unsigned_integer();
        expect(",");
        expect_string("localPosition");
        expect(":");
        result.local_position = vector();
        expect(",");
        expect_string("localRotation");
        expect(":");
        result.local_rotation = vector();
        if (consume("}")) return result;
        expect(",");
        expect_string("taskInventorySerial");
        expect(":");
        const auto task_inventory_serial = unsigned_integer();
        if (task_inventory_serial > 65535) fail("task inventory serial is outside the supported range");
        result.task_inventory_serial = static_cast<std::uint16_t>(task_inventory_serial);
        expect(",");
        expect_string("taskInventory");
        expect(":");
        expect("[");
        if (!consume("]")) {
            do result.task_inventory.push_back(task_inventory_item()); while (consume(","));
            expect("]");
        }
        expect("}");
        return result;
    }

    std::string_view input_;
    std::size_t position_{};
};

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
                std::to_string(entity->velocity.y) + ',' + std::to_string(entity->velocity.z) +
                "],\"objectId\":" + api::json_string(entity->object_id) +
                ",\"ownerId\":" + api::json_string(entity->owner_id) +
                ",\"scale\":[" + std::to_string(entity->scale.x) + ',' +
                std::to_string(entity->scale.y) + ',' + std::to_string(entity->scale.z) +
                "],\"material\":" + std::to_string(entity->material) +
                ",\"creatorId\":" + api::json_string(entity->creator_id) +
                ",\"basePermissions\":" + std::to_string(entity->base_permissions) +
                ",\"ownerPermissions\":" + std::to_string(entity->owner_permissions) +
                ",\"groupPermissions\":" + std::to_string(entity->group_permissions) +
                ",\"everyonePermissions\":" + std::to_string(entity->everyone_permissions) +
                ",\"nextOwnerPermissions\":" + std::to_string(entity->next_owner_permissions) +
                ",\"creationDate\":" + std::to_string(entity->creation_date) +
                ",\"rotation\":[" + std::to_string(entity->rotation.x) + ',' +
                std::to_string(entity->rotation.y) + ',' + std::to_string(entity->rotation.z) +
                "],\"description\":" + api::json_string(entity->description) +
                ",\"avatarFlying\":" + (entity->avatar_flying ? "true" : "false") +
                ",\"physical\":" + (entity->physical ? "true" : "false") +
                ",\"phantom\":" + (entity->phantom ? "true" : "false") +
                ",\"physicsShapeType\":" + std::to_string(entity->physics_shape_type) +
                ",\"physicsDensity\":" + std::to_string(entity->physics_density) +
                ",\"physicsFriction\":" + std::to_string(entity->physics_friction) +
                ",\"physicsRestitution\":" + std::to_string(entity->physics_restitution) +
                ",\"physicsGravityMultiplier\":" +
                    std::to_string(entity->physics_gravity_multiplier) +
                ",\"textureEntry\":" + api::json_string(bytes_to_hex(entity->texture_entry)) +
                ",\"pathCurve\":" + std::to_string(entity->path_curve) +
                ",\"profileCurve\":" + std::to_string(entity->profile_curve) +
                ",\"pathBegin\":" + std::to_string(entity->path_begin) +
                ",\"pathEnd\":" + std::to_string(entity->path_end) +
                ",\"pathScaleX\":" + std::to_string(entity->path_scale_x) +
                ",\"pathScaleY\":" + std::to_string(entity->path_scale_y) +
                ",\"pathShearX\":" + std::to_string(entity->path_shear_x) +
                ",\"pathShearY\":" + std::to_string(entity->path_shear_y) +
                ",\"pathTwist\":" + std::to_string(entity->path_twist) +
                ",\"pathTwistBegin\":" + std::to_string(entity->path_twist_begin) +
                ",\"pathRadiusOffset\":" + std::to_string(entity->path_radius_offset) +
                ",\"pathTaperX\":" + std::to_string(entity->path_taper_x) +
                ",\"pathTaperY\":" + std::to_string(entity->path_taper_y) +
                ",\"pathRevolutions\":" + std::to_string(entity->path_revolutions) +
                ",\"pathSkew\":" + std::to_string(entity->path_skew) +
                ",\"profileBegin\":" + std::to_string(entity->profile_begin) +
                ",\"profileEnd\":" + std::to_string(entity->profile_end) +
                ",\"profileHollow\":" + std::to_string(entity->profile_hollow) +
                ",\"parentId\":" + std::to_string(entity->parent_id) +
                ",\"localPosition\":[" + std::to_string(entity->local_position.x) + ',' +
                std::to_string(entity->local_position.y) + ',' +
                std::to_string(entity->local_position.z) +
                "],\"localRotation\":[" + std::to_string(entity->local_rotation.x) + ',' +
                std::to_string(entity->local_rotation.y) + ',' +
                std::to_string(entity->local_rotation.z) +
                "],\"taskInventorySerial\":" + std::to_string(entity->task_inventory_serial) +
                ",\"taskInventory\":[";
        bool first_task_item = true;
        for (const auto& item : entity->task_inventory) {
            if (!first_task_item) json += ',';
            first_task_item = false;
            json += "{\"itemId\":" + api::json_string(item.item_id) +
                ",\"assetId\":" + api::json_string(item.asset_id) +
                ",\"creatorId\":" + api::json_string(item.creator_id) +
                ",\"ownerId\":" + api::json_string(item.owner_id) +
                ",\"lastOwnerId\":" + api::json_string(item.last_owner_id) +
                ",\"groupId\":" + api::json_string(item.group_id) +
                ",\"name\":" + api::json_string(item.name) +
                ",\"description\":" + api::json_string(item.description) +
                ",\"assetType\":" + std::to_string(item.asset_type) +
                ",\"inventoryType\":" + std::to_string(item.inventory_type) +
                ",\"flags\":" + std::to_string(item.flags) +
                ",\"basePermissions\":" + std::to_string(item.base_permissions) +
                ",\"currentPermissions\":" + std::to_string(item.current_permissions) +
                ",\"groupPermissions\":" + std::to_string(item.group_permissions) +
                ",\"everyonePermissions\":" + std::to_string(item.everyone_permissions) +
                ",\"nextPermissions\":" + std::to_string(item.next_permissions) +
                ",\"saleType\":" + std::to_string(item.sale_type) +
                ",\"salePrice\":" + std::to_string(item.sale_price) +
                ",\"creationDate\":" + std::to_string(item.creation_date) + '}';
        }
        json += "]}";
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
                       "viewer_id TEXT PRIMARY KEY, creator_id TEXT NOT NULL CHECK(length(creator_id) = 36),"
                       "sha256 TEXT NOT NULL, size INTEGER NOT NULL,"
                       "created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP);"
                       "CREATE INDEX IF NOT EXISTS asset_mappings_sha256 ON asset_mappings(sha256);"
                       "CREATE TABLE IF NOT EXISTS baked_texture_cache ("
                       "cache_id TEXT NOT NULL, texture_index INTEGER NOT NULL, asset_id TEXT NOT NULL,"
                       "updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,"
                       "PRIMARY KEY (cache_id, texture_index),"
                       "FOREIGN KEY (asset_id) REFERENCES asset_mappings(viewer_id));");
    if (!has_column(database_, "asset_mappings", "creator_id"))
        execute(database_, "ALTER TABLE asset_mappings ADD COLUMN creator_id TEXT NOT NULL "
                           "DEFAULT '00000000-0000-0000-0000-000000000000' CHECK(length(creator_id) = 36);");
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

bool RegionStorage::load_snapshot(scene::Scene& scene) const {
    const auto metadata = snapshot_metadata();
    const auto relative = std::filesystem::path(metadata.path);
    if (relative.is_absolute() || std::find(relative.begin(), relative.end(), "..") != relative.end()) {
        throw std::runtime_error("scene snapshot path is outside the region data directory");
    }
    const auto path = data_path_ / relative;
    if (!std::filesystem::exists(path)) {
        if (metadata.revision == 0) return false;
        throw std::runtime_error("committed scene snapshot is missing");
    }
    std::ifstream input(path, std::ios::binary);
    input.exceptions(std::ios::badbit);
    const std::string contents((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    auto [revision, entities] = SnapshotReader(contents).read();
    if (revision != metadata.revision) {
        throw std::runtime_error("scene snapshot revision does not match committed metadata");
    }
    scene.restore(revision, std::move(entities));
    return true;
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

AssetMetadata RegionStorage::store_asset(std::string viewer_id, std::string creator_id,
                                         std::span<const std::byte> content) {
    if (!valid_uuid(viewer_id)) throw std::invalid_argument("asset viewer ID must be a UUID");
    if (!valid_uuid(creator_id)) throw std::invalid_argument("asset creator ID must be a UUID");
    AssetMetadata metadata{
        std::move(viewer_id), std::move(creator_id), crypto::sha256_hex(content), content.size()};
    if (const auto existing = find_asset(metadata.viewer_id)) {
        constexpr std::string_view unknown_creator = "00000000-0000-0000-0000-000000000000";
        if (existing->creator_id == unknown_creator && metadata.creator_id != unknown_creator &&
            existing->sha256 == metadata.sha256 && existing->size == metadata.size) {
            sqlite3_stmt* statement = nullptr;
            if (sqlite3_prepare_v2(database_,
                                   "UPDATE asset_mappings SET creator_id = ? "
                                   "WHERE viewer_id = ? AND creator_id = ?",
                                   -1, &statement, nullptr) != SQLITE_OK)
                throw std::runtime_error(sqlite3_errmsg(database_));
            sqlite3_bind_text(statement, 1, metadata.creator_id.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(statement, 2, metadata.viewer_id.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(statement, 3, unknown_creator.data(),
                              static_cast<int>(unknown_creator.size()), SQLITE_STATIC);
            const auto result = sqlite3_step(statement);
            sqlite3_finalize(statement);
            if (result != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_));
            return metadata;
        }
        if (existing->creator_id != metadata.creator_id || existing->sha256 != metadata.sha256 ||
            existing->size != metadata.size)
            throw std::invalid_argument("asset viewer ID " + metadata.viewer_id +
                                        " is already mapped to creator " + existing->creator_id +
                                        ", hash " + existing->sha256 + ", size " +
                                        std::to_string(existing->size) + "; attempted creator " +
                                        metadata.creator_id + ", hash " + metadata.sha256 + ", size " +
                                        std::to_string(metadata.size));
        return *existing;
    }
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
    const char* sql = "INSERT INTO asset_mappings (viewer_id, creator_id, sha256, size) VALUES (?, ?, ?, ?) "
                      "ON CONFLICT(viewer_id) DO NOTHING";
    if (sqlite3_prepare_v2(database_, sql, -1, &statement, nullptr) != SQLITE_OK) {
        throw std::runtime_error(sqlite3_errmsg(database_));
    }
    sqlite3_bind_text(statement, 1, metadata.viewer_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 2, metadata.creator_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 3, metadata.sha256.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(statement, 4, static_cast<sqlite3_int64>(metadata.size));
    const auto result = sqlite3_step(statement);
    sqlite3_finalize(statement);
    if (result != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_));
    return metadata;
}

AssetMetadata RegionStorage::reconcile_asset_creator(std::string_view viewer_id,
                                                      std::string_view creator_id,
                                                      std::string_view sha256,
                                                      std::uint64_t size) {
    if (!valid_uuid(viewer_id)) throw std::invalid_argument("asset viewer ID must be a UUID");
    if (!valid_uuid(creator_id)) throw std::invalid_argument("asset creator ID must be a UUID");
    const auto existing = find_asset(viewer_id);
    if (!existing || existing->sha256 != sha256 || existing->size != size)
        throw std::invalid_argument("asset content identity does not match reconciliation request");
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(database_, "UPDATE asset_mappings SET creator_id = ? WHERE viewer_id = ?", -1,
                           &statement, nullptr) != SQLITE_OK)
        throw std::runtime_error(sqlite3_errmsg(database_));
    sqlite3_bind_text(statement, 1, creator_id.data(), static_cast<int>(creator_id.size()), SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 2, viewer_id.data(), static_cast<int>(viewer_id.size()), SQLITE_TRANSIENT);
    const auto result = sqlite3_step(statement);
    sqlite3_finalize(statement);
    if (result != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_));
    return {std::string(viewer_id), std::string(creator_id), std::string(sha256), size};
}

void RegionStorage::store_baked_texture(std::string cache_id, std::uint8_t texture_index,
                                        std::string asset_id) {
    sqlite3_stmt* statement = nullptr;
    const char* sql = "INSERT INTO baked_texture_cache (cache_id, texture_index, asset_id) VALUES (?, ?, ?) "
                      "ON CONFLICT(cache_id, texture_index) DO UPDATE SET "
                      "asset_id=excluded.asset_id, updated_at=CURRENT_TIMESTAMP";
    if (sqlite3_prepare_v2(database_, sql, -1, &statement, nullptr) != SQLITE_OK)
        throw std::runtime_error(sqlite3_errmsg(database_));
    sqlite3_bind_text(statement, 1, cache_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(statement, 2, texture_index);
    sqlite3_bind_text(statement, 3, asset_id.c_str(), -1, SQLITE_TRANSIENT);
    const auto result = sqlite3_step(statement);
    sqlite3_finalize(statement);
    if (result != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_));
}

std::optional<std::string> RegionStorage::find_baked_texture(
    std::string_view cache_id, std::uint8_t texture_index) const {
    sqlite3_stmt* statement = nullptr;
    const char* sql = "SELECT cache.asset_id FROM baked_texture_cache cache "
                      "JOIN asset_mappings asset ON asset.viewer_id = cache.asset_id "
                      "WHERE cache.cache_id = ? AND cache.texture_index = ?";
    if (sqlite3_prepare_v2(database_, sql, -1, &statement, nullptr) != SQLITE_OK)
        throw std::runtime_error(sqlite3_errmsg(database_));
    sqlite3_bind_text(statement, 1, cache_id.data(), static_cast<int>(cache_id.size()), SQLITE_TRANSIENT);
    sqlite3_bind_int(statement, 2, texture_index);
    std::optional<std::string> result;
    if (sqlite3_step(statement) == SQLITE_ROW)
        result = reinterpret_cast<const char*>(sqlite3_column_text(statement, 0));
    sqlite3_finalize(statement);
    return result;
}

std::size_t RegionStorage::import_asset_directory(const std::filesystem::path& directory,
                                                  std::string_view creator_id) {
    if (!std::filesystem::is_directory(directory)) return 0;
    std::size_t imported = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(directory)) {
        if (!entry.is_regular_file() ||
            (entry.path().extension() != ".j2c" && entry.path().extension() != ".ogg" &&
             entry.path().extension() != ".bodypart" && entry.path().extension() != ".clothing"))
            continue;
        std::ifstream input(entry.path(), std::ios::binary | std::ios::ate);
        if (!input) throw std::runtime_error("asset source file could not be opened");
        const auto length = static_cast<std::streamsize>(input.tellg());
        if (length <= 0) throw std::runtime_error("asset source file was empty");
        input.seekg(0);
        std::vector<std::byte> content(static_cast<std::size_t>(length));
        input.read(reinterpret_cast<char*>(content.data()), length);
        store_asset(entry.path().stem().string(), std::string(creator_id), content);
        ++imported;
    }
    return imported;
}

std::optional<AssetMetadata> RegionStorage::find_asset(std::string_view viewer_id) const {
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(database_, "SELECT viewer_id, creator_id, sha256, size FROM asset_mappings WHERE viewer_id = ?", -1,
                           &statement, nullptr) != SQLITE_OK) {
        throw std::runtime_error(sqlite3_errmsg(database_));
    }
    sqlite3_bind_text(statement, 1, viewer_id.data(), static_cast<int>(viewer_id.size()), SQLITE_TRANSIENT);
    std::optional<AssetMetadata> metadata;
    if (sqlite3_step(statement) == SQLITE_ROW) {
        metadata = AssetMetadata{
            reinterpret_cast<const char*>(sqlite3_column_text(statement, 0)),
            reinterpret_cast<const char*>(sqlite3_column_text(statement, 1)),
            reinterpret_cast<const char*>(sqlite3_column_text(statement, 2)),
            static_cast<std::uint64_t>(sqlite3_column_int64(statement, 3))};
    }
    sqlite3_finalize(statement);
    return metadata;
}

std::vector<AssetMetadata> RegionStorage::list_assets() const {
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(database_,
                           "SELECT viewer_id, creator_id, sha256, size FROM asset_mappings ORDER BY viewer_id",
                           -1, &statement, nullptr) != SQLITE_OK) {
        throw std::runtime_error(sqlite3_errmsg(database_));
    }
    std::vector<AssetMetadata> assets;
    int result = SQLITE_ROW;
    while ((result = sqlite3_step(statement)) == SQLITE_ROW) {
        assets.push_back(AssetMetadata{
            reinterpret_cast<const char*>(sqlite3_column_text(statement, 0)),
            reinterpret_cast<const char*>(sqlite3_column_text(statement, 1)),
            reinterpret_cast<const char*>(sqlite3_column_text(statement, 2)),
            static_cast<std::uint64_t>(sqlite3_column_int64(statement, 3))});
    }
    sqlite3_finalize(statement);
    if (result != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_));
    return assets;
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
