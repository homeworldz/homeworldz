#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace homeworldz::parcel {

// ParcelFlags bitfield (indra / OpenMetaverse ParcelManager.ParcelFlags).
enum ParcelFlags : std::uint32_t {
    flag_allow_fly = 1U << 0,
    flag_allow_other_scripts = 1U << 1,
    flag_for_sale = 1U << 2,
    flag_allow_landmark = 1U << 3,
    flag_allow_terraform = 1U << 4,
    flag_allow_damage = 1U << 5,
    flag_create_objects = 1U << 6,
    flag_for_sale_objects = 1U << 7,
    flag_use_access_group = 1U << 8,
    flag_use_access_list = 1U << 9,
    flag_use_ban_list = 1U << 10,
    flag_use_pass_list = 1U << 11,
    flag_show_directory = 1U << 12,
    flag_allow_deed_to_group = 1U << 13,
    flag_contribute_with_deed = 1U << 14,
    flag_sound_local = 1U << 15,
    flag_sell_parcel_objects = 1U << 16,
    flag_allow_publish = 1U << 17,
    flag_mature_publish = 1U << 18,
    flag_url_web_page = 1U << 19,
    flag_url_raw_html = 1U << 20,
    flag_restrict_push_object = 1U << 21,
    flag_deny_anonymous = 1U << 22,
    flag_linden_home = 1U << 23,
    flag_allow_group_scripts = 1U << 25,
    flag_create_group_objects = 1U << 26,
    flag_allow_primitive_entry = 1U << 27,
    flag_allow_group_object_entry = 1U << 28,
    flag_allow_voice_chat = 1U << 29,
    flag_use_estate_voice_channel = 1U << 30,
    flag_deny_age_unverified = 1U << 31,
};

// Default flags for a freshly created parcel (LandData.cs defaults).
constexpr std::uint32_t default_parcel_flags =
    flag_allow_fly | flag_allow_landmark | flag_allow_primitive_entry |
    flag_allow_deed_to_group | flag_create_objects | flag_allow_other_scripts |
    flag_sound_local | flag_allow_voice_chat;

// LandingType (server semantics): 0 = blocked, 1 = landing point, 2 = anywhere.
enum class LandingType : std::uint8_t { blocked = 0, landing_point = 1, anywhere = 2 };

// AccessList per-entry flags and request filter.
enum AccessFlags : std::uint32_t {
    access_none = 0,
    access_allowed = 1U << 0,
    access_ban = 1U << 1,
    access_both = access_allowed | access_ban,
};

// ParcelResult sequence-id sentinels handed back in ParcelProperties.RequestResult.
constexpr std::int32_t result_no_data = -1;
constexpr std::int32_t result_single = 0;
constexpr std::int32_t result_multiple = 1;

// Sequence id used when the viewer explicitly selected a parcel.
constexpr std::int32_t selected_parcel_sequence_id = -10000;

// First LocalID assigned within a region.
constexpr std::int32_t first_local_id = 1;

struct Vector3 {
    float x{};
    float y{};
    float z{};
};

struct AccessEntry {
    std::string agent_id;   // UUID string
    std::int32_t time{};    // time_t (expiry / added); 0 = no expiry
    std::uint32_t flags{};  // AccessFlags (Access / Ban)
};

struct Parcel {
    std::string global_id;   // persistent UUID (storage key)
    std::int32_t local_id{};
    std::string name{"Parcel"};
    std::string description;
    std::string owner_id;    // UUID string
    std::string group_id;    // UUID string, empty when none
    bool is_group_owned{};
    std::uint32_t flags{default_parcel_flags};
    std::int8_t category{};  // ParcelCategory
    std::int32_t sale_price{};
    std::string auth_buyer_id;
    std::string snapshot_id;
    std::string media_id;
    std::string media_url;
    std::string music_url;
    std::uint8_t media_auto_scale{};
    std::int32_t pass_price{};
    float pass_hours{};
    Vector3 user_location;
    Vector3 user_look_at;
    std::int32_t other_clean_time{};
    std::int32_t claim_date{};
    std::uint8_t landing_type{static_cast<std::uint8_t>(LandingType::anywhere)};
    // Packed 1-bit-per-4m-cell coverage map, row-major (n = y*edge + x), LSB-first.
    std::vector<std::uint8_t> bitmap;
    std::vector<AccessEntry> access;

    // Number of set cells * 16 m^2.
    std::int32_t area(int edge_cells) const;
    bool contains_cell(int edge_cells, int cell_x, int cell_y) const;
    // Inclusive-min / exclusive-max cell AABB of the set cells; false when empty.
    bool cell_bounds(int edge_cells, int& min_x, int& min_y, int& max_x, int& max_y) const;
};

// A region's complete set of parcels plus the derived per-cell ownership grid.
// Cell resolution is fixed at 4 m; edge_cells = region_size_metres / 4.
class ParcelSet {
public:
    // Build a single default parcel covering the whole region, owned by owner_id.
    ParcelSet(int region_size_metres, std::string global_id, std::string owner_id,
              std::int32_t claim_date = 0);
    // Build from persisted parcels (bitmaps must match this region size).
    ParcelSet(int region_size_metres, std::vector<Parcel> parcels);

    int region_size_metres() const { return region_size_; }
    int edge_cells() const { return edge_cells_; }
    int bitmap_bytes() const { return (edge_cells_ * edge_cells_ + 7) / 8; }

    const std::vector<Parcel>& parcels() const { return parcels_; }
    std::vector<Parcel>& parcels() { return parcels_; }

    Parcel* find_by_local_id(std::int32_t local_id);
    const Parcel* find_by_local_id(std::int32_t local_id) const;

    // Parcel covering a world-metre point, or nullptr if outside the region.
    const Parcel* parcel_at(float x, float y) const;

    // The single parcel covering an entire (west,south,east,north) metre rectangle,
    // or nullptr when the rectangle spans more than one parcel or lies outside.
    const Parcel* parcel_covering(float west, float south, float east, float north) const;

    // Subdivide: carve the rectangle (which must lie within exactly one parcel and
    // not equal the whole parcel) into a new parcel with new_global_id owned by
    // owner_id. Returns the new parcel's local id, or nullopt on invalid geometry.
    std::optional<std::int32_t> divide(float west, float south, float east, float north,
                                       std::string new_global_id, std::string owner_id,
                                       std::int32_t claim_date);

    // Join every parcel intersecting the rectangle into one. All must share owner_id.
    // Returns the surviving local id, or nullopt when the geometry/ownership is invalid.
    std::optional<std::int32_t> join(float west, float south, float east, float north,
                                     std::string_view owner_id);

    // Bit helpers on a packed bitmap of the given edge size.
    static bool bit_get(const std::vector<std::uint8_t>& bitmap, int edge_cells,
                        int cell_x, int cell_y);
    static void bit_set(std::vector<std::uint8_t>& bitmap, int edge_cells, int cell_x,
                        int cell_y, bool value);
    static std::vector<std::uint8_t> full_bitmap(int edge_cells);
    static std::vector<std::uint8_t> rectangle_bitmap(int edge_cells, int west, int south,
                                                      int east, int north);

private:
    std::int32_t next_local_id();

    int region_size_{256};
    int edge_cells_{64};
    std::int32_t last_local_id_{first_local_id - 1};
    std::vector<Parcel> parcels_;
};

} // namespace homeworldz::parcel
