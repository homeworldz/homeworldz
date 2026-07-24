#include "homeworldz/parcel.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace homeworldz::parcel {
namespace {

int cells_for(int region_size_metres) {
    if (region_size_metres <= 0 || region_size_metres % 4 != 0)
        throw std::invalid_argument("region size must be a positive multiple of 4 metres");
    return region_size_metres / 4;
}

// Clamp a world-metre coordinate to a cell index within [0, edge).
int metre_to_cell(float metre, int edge_cells) {
    int cell = static_cast<int>(std::floor(metre / 4.0F));
    if (cell < 0) cell = 0;
    if (cell >= edge_cells) cell = edge_cells - 1;
    return cell;
}

} // namespace

std::int32_t Parcel::area(int edge_cells) const {
    std::int32_t set = 0;
    for (int y = 0; y < edge_cells; ++y)
        for (int x = 0; x < edge_cells; ++x)
            if (contains_cell(edge_cells, x, y)) ++set;
    return set * 16;
}

bool Parcel::contains_cell(int edge_cells, int cell_x, int cell_y) const {
    return ParcelSet::bit_get(bitmap, edge_cells, cell_x, cell_y);
}

bool Parcel::cell_bounds(int edge_cells, int& min_x, int& min_y, int& max_x, int& max_y) const {
    bool any = false;
    min_x = min_y = edge_cells;
    max_x = max_y = -1;
    for (int y = 0; y < edge_cells; ++y)
        for (int x = 0; x < edge_cells; ++x)
            if (contains_cell(edge_cells, x, y)) {
                any = true;
                min_x = std::min(min_x, x);
                min_y = std::min(min_y, y);
                max_x = std::max(max_x, x + 1);
                max_y = std::max(max_y, y + 1);
            }
    return any;
}

bool ParcelSet::bit_get(const std::vector<std::uint8_t>& bitmap, int edge_cells,
                        int cell_x, int cell_y) {
    if (cell_x < 0 || cell_y < 0 || cell_x >= edge_cells || cell_y >= edge_cells) return false;
    const std::size_t index = static_cast<std::size_t>(cell_y) * edge_cells + cell_x;
    const std::size_t byte = index >> 3;
    if (byte >= bitmap.size()) return false;
    return (bitmap[byte] & (1U << (index & 7U))) != 0;
}

void ParcelSet::bit_set(std::vector<std::uint8_t>& bitmap, int edge_cells, int cell_x,
                        int cell_y, bool value) {
    if (cell_x < 0 || cell_y < 0 || cell_x >= edge_cells || cell_y >= edge_cells) return;
    const std::size_t index = static_cast<std::size_t>(cell_y) * edge_cells + cell_x;
    const std::size_t byte = index >> 3;
    if (byte >= bitmap.size()) bitmap.resize(byte + 1, 0);
    if (value) bitmap[byte] |= static_cast<std::uint8_t>(1U << (index & 7U));
    else bitmap[byte] &= static_cast<std::uint8_t>(~(1U << (index & 7U)));
}

std::vector<std::uint8_t> ParcelSet::full_bitmap(int edge_cells) {
    return rectangle_bitmap(edge_cells, 0, 0, edge_cells * 4, edge_cells * 4);
}

std::vector<std::uint8_t> ParcelSet::rectangle_bitmap(int edge_cells, int west, int south,
                                                     int east, int north) {
    std::vector<std::uint8_t> bitmap((edge_cells * edge_cells + 7) / 8, 0);
    const int start_x = std::max(0, west / 4);
    const int start_y = std::max(0, south / 4);
    const int end_x = std::min(edge_cells, east / 4);
    const int end_y = std::min(edge_cells, north / 4);
    for (int y = start_y; y < end_y; ++y)
        for (int x = start_x; x < end_x; ++x)
            bit_set(bitmap, edge_cells, x, y, true);
    return bitmap;
}

ParcelSet::ParcelSet(int region_size_metres, std::string global_id, std::string owner_id,
                     std::int32_t claim_date)
    : region_size_(region_size_metres), edge_cells_(cells_for(region_size_metres)) {
    Parcel parcel;
    parcel.global_id = std::move(global_id);
    parcel.local_id = next_local_id();
    parcel.name = "HomeWorldz";
    parcel.owner_id = std::move(owner_id);
    parcel.flags = default_parcel_flags;
    parcel.claim_date = claim_date;
    parcel.bitmap = full_bitmap(edge_cells_);
    parcels_.push_back(std::move(parcel));
}

ParcelSet::ParcelSet(int region_size_metres, std::vector<Parcel> parcels)
    : region_size_(region_size_metres), edge_cells_(cells_for(region_size_metres)),
      parcels_(std::move(parcels)) {
    const int bytes = bitmap_bytes();
    for (auto& parcel : parcels_) {
        if (static_cast<int>(parcel.bitmap.size()) < bytes) parcel.bitmap.resize(bytes, 0);
        last_local_id_ = std::max(last_local_id_, parcel.local_id);
    }
}

std::int32_t ParcelSet::next_local_id() { return ++last_local_id_; }

Parcel* ParcelSet::find_by_local_id(std::int32_t local_id) {
    for (auto& parcel : parcels_)
        if (parcel.local_id == local_id) return &parcel;
    return nullptr;
}

const Parcel* ParcelSet::find_by_local_id(std::int32_t local_id) const {
    for (const auto& parcel : parcels_)
        if (parcel.local_id == local_id) return &parcel;
    return nullptr;
}

const Parcel* ParcelSet::parcel_at(float x, float y) const {
    if (x < 0.0F || y < 0.0F || x >= static_cast<float>(region_size_) ||
        y >= static_cast<float>(region_size_))
        return nullptr;
    const int cell_x = metre_to_cell(x, edge_cells_);
    const int cell_y = metre_to_cell(y, edge_cells_);
    for (const auto& parcel : parcels_)
        if (parcel.contains_cell(edge_cells_, cell_x, cell_y)) return &parcel;
    return nullptr;
}

const Parcel* ParcelSet::parcel_covering(float west, float south, float east, float north) const {
    const int start_x = std::max(0, static_cast<int>(std::floor(west / 4.0F)));
    const int start_y = std::max(0, static_cast<int>(std::floor(south / 4.0F)));
    const int end_x = std::min(edge_cells_, static_cast<int>(std::ceil(east / 4.0F)));
    const int end_y = std::min(edge_cells_, static_cast<int>(std::ceil(north / 4.0F)));
    if (start_x >= end_x || start_y >= end_y) return nullptr;
    const Parcel* found = nullptr;
    for (int y = start_y; y < end_y; ++y)
        for (int x = start_x; x < end_x; ++x) {
            const Parcel* here = nullptr;
            for (const auto& parcel : parcels_)
                if (parcel.contains_cell(edge_cells_, x, y)) {
                    here = &parcel;
                    break;
                }
            if (here == nullptr) return nullptr;
            if (found == nullptr) found = here;
            else if (found != here) return nullptr;
        }
    return found;
}

std::optional<std::int32_t> ParcelSet::divide(float west, float south, float east, float north,
                                              std::string new_global_id, std::string owner_id,
                                              std::int32_t claim_date) {
    const int start_x = static_cast<int>(std::floor(std::min(west, east) / 4.0F));
    const int start_y = static_cast<int>(std::floor(std::min(south, north) / 4.0F));
    const int end_x = static_cast<int>(std::ceil(std::max(west, east) / 4.0F));
    const int end_y = static_cast<int>(std::ceil(std::max(south, north) / 4.0F));
    if (start_x < 0 || start_y < 0 || end_x > edge_cells_ || end_y > edge_cells_) return std::nullopt;
    if (start_x >= end_x || start_y >= end_y) return std::nullopt;

    // The rectangle must lie entirely within exactly one parcel.
    Parcel* source = nullptr;
    for (int y = start_y; y < end_y; ++y)
        for (int x = start_x; x < end_x; ++x) {
            Parcel* here = nullptr;
            for (auto& parcel : parcels_)
                if (parcel.contains_cell(edge_cells_, x, y)) {
                    here = &parcel;
                    break;
                }
            if (here == nullptr) return std::nullopt;
            if (source == nullptr) source = here;
            else if (source != here) return std::nullopt;
        }
    if (source == nullptr) return std::nullopt;
    // Refuse to "divide" a rectangle covering the whole source parcel.
    if (source->area(edge_cells_) == (end_x - start_x) * (end_y - start_y) * 16)
        return std::nullopt;

    Parcel carved;
    carved.global_id = std::move(new_global_id);
    carved.local_id = next_local_id();
    carved.name = "Parcel";
    carved.owner_id = std::move(owner_id);
    carved.flags = default_parcel_flags;
    carved.claim_date = claim_date;
    carved.bitmap.assign(bitmap_bytes(), 0);
    for (int y = start_y; y < end_y; ++y)
        for (int x = start_x; x < end_x; ++x) {
            bit_set(carved.bitmap, edge_cells_, x, y, true);
            bit_set(source->bitmap, edge_cells_, x, y, false);
        }
    const std::int32_t local_id = carved.local_id;
    parcels_.push_back(std::move(carved));
    return local_id;
}

std::optional<std::int32_t> ParcelSet::join(float west, float south, float east, float north,
                                            std::string_view owner_id) {
    const int start_x = std::max(0, static_cast<int>(std::floor(std::min(west, east) / 4.0F)));
    const int start_y = std::max(0, static_cast<int>(std::floor(std::min(south, north) / 4.0F)));
    const int end_x = std::min(edge_cells_, static_cast<int>(std::ceil(std::max(west, east) / 4.0F)));
    const int end_y = std::min(edge_cells_, static_cast<int>(std::ceil(std::max(south, north) / 4.0F)));
    if (start_x >= end_x || start_y >= end_y) return std::nullopt;

    // Collect the distinct parcels intersecting the rectangle.
    std::vector<std::int32_t> touched;
    for (int y = start_y; y < end_y; ++y)
        for (int x = start_x; x < end_x; ++x)
            for (auto& parcel : parcels_)
                if (parcel.contains_cell(edge_cells_, x, y)) {
                    if (std::find(touched.begin(), touched.end(), parcel.local_id) == touched.end())
                        touched.push_back(parcel.local_id);
                    break;
                }
    if (touched.size() < 2) return std::nullopt;
    // Every touched parcel must share the requested owner.
    for (const auto local_id : touched) {
        const Parcel* parcel = find_by_local_id(local_id);
        if (parcel == nullptr || parcel->owner_id != owner_id) return std::nullopt;
    }

    const std::int32_t master_id = *std::min_element(touched.begin(), touched.end());
    Parcel* master = find_by_local_id(master_id);
    if (master == nullptr) return std::nullopt;
    for (const auto local_id : touched) {
        if (local_id == master_id) continue;
        const Parcel* other = find_by_local_id(local_id);
        if (other == nullptr) continue;
        for (int y = 0; y < edge_cells_; ++y)
            for (int x = 0; x < edge_cells_; ++x)
                if (bit_get(other->bitmap, edge_cells_, x, y))
                    bit_set(master->bitmap, edge_cells_, x, y, true);
    }
    parcels_.erase(std::remove_if(parcels_.begin(), parcels_.end(),
                       [&](const Parcel& parcel) {
                           return parcel.local_id != master_id &&
                                  std::find(touched.begin(), touched.end(), parcel.local_id) !=
                                      touched.end();
                       }),
                   parcels_.end());
    return master_id;
}

} // namespace homeworldz::parcel
