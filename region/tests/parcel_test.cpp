#include "homeworldz/parcel.h"

#include <cstdio>

using homeworldz::parcel::Parcel;
using homeworldz::parcel::ParcelSet;

namespace {

int failures = 0;

void check(bool condition, const char* label) {
    if (!condition) {
        std::printf("FAIL: %s\n", label);
        ++failures;
    }
}

} // namespace

int main() {
    // Default region-wide parcel for a 256 m region.
    {
        ParcelSet set(256, "11111111-1111-4111-8111-111111111111",
                      "22222222-2222-4222-8222-222222222222", 1000);
        check(set.edge_cells() == 64, "256m region has 64 cells per edge");
        check(set.bitmap_bytes() == 512, "256m region bitmap is 512 bytes");
        check(set.parcels().size() == 1, "fresh region has one parcel");
        const Parcel& parcel = set.parcels().front();
        check(parcel.local_id == 1, "default parcel local id is 1");
        check(parcel.owner_id == "22222222-2222-4222-8222-222222222222", "default owner set");
        check(parcel.area(set.edge_cells()) == 65536, "whole 256m parcel is 65536 m^2");
        check(set.parcel_at(128.0F, 128.0F) == &parcel, "point in region resolves to parcel");
        check(set.parcel_at(300.0F, 10.0F) == nullptr, "point outside region resolves to null");
        check(set.parcel_covering(0.0F, 0.0F, 256.0F, 256.0F) == &parcel, "whole-region rect covered");
    }

    // Variable region size: 512 m -> 128 cells, 2048-byte bitmap.
    {
        ParcelSet set(512, "aaaa1111-1111-4111-8111-111111111111",
                      "bbbb2222-2222-4222-8222-222222222222", 0);
        check(set.edge_cells() == 128, "512m region has 128 cells per edge");
        check(set.bitmap_bytes() == 2048, "512m region bitmap is 2048 bytes");
        check(set.parcels().front().area(set.edge_cells()) == 262144, "whole 512m parcel is 262144 m^2");
    }

    // Subdivide the SW 64x64 m corner out of the default parcel.
    {
        ParcelSet set(256, "11111111-1111-4111-8111-111111111111",
                      "22222222-2222-4222-8222-222222222222", 1000);
        const auto carved = set.divide(0.0F, 0.0F, 64.0F, 64.0F,
                                       "33333333-3333-4333-8333-333333333333",
                                       "44444444-4444-4444-8444-444444444444", 2000);
        check(carved.has_value(), "divide returns a new local id");
        check(set.parcels().size() == 2, "region now has two parcels");
        if (carved) {
            const Parcel* small = set.find_by_local_id(*carved);
            check(small != nullptr, "carved parcel is findable");
            check(small && small->area(set.edge_cells()) == 64 * 64, "carved parcel is 4096 m^2");
            check(small && small->owner_id == "44444444-4444-4444-8444-444444444444",
                  "carved parcel keeps requested owner");
            check(set.parcel_at(10.0F, 10.0F) == small, "SW point resolves to carved parcel");
        }
        const Parcel* original = set.find_by_local_id(1);
        check(original && original->area(set.edge_cells()) == 65536 - 4096,
              "original parcel shrank by the carved area");
        check(set.parcel_at(200.0F, 200.0F) == original, "NE point still resolves to original");

        // Dividing along a boundary that spans both parcels must fail.
        const auto spanning = set.divide(0.0F, 0.0F, 128.0F, 128.0F,
                                         "55555555-5555-4555-8555-555555555555",
                                         "44444444-4444-4444-8444-444444444444", 3000);
        check(!spanning.has_value(), "divide spanning two parcels is rejected");
    }

    // Join two same-owner parcels back into one.
    {
        ParcelSet set(256, "11111111-1111-4111-8111-111111111111",
                      "22222222-2222-4222-8222-222222222222", 1000);
        // Carve, but keep the same owner so join is permitted.
        const auto carved = set.divide(0.0F, 0.0F, 64.0F, 64.0F,
                                       "33333333-3333-4333-8333-333333333333",
                                       "22222222-2222-4222-8222-222222222222", 2000);
        check(carved.has_value() && set.parcels().size() == 2, "prepared two same-owner parcels");
        const auto merged = set.join(0.0F, 0.0F, 256.0F, 256.0F,
                                     "22222222-2222-4222-8222-222222222222");
        check(merged.has_value(), "join returns surviving local id");
        check(set.parcels().size() == 1, "join collapses back to one parcel");
        check(merged && set.find_by_local_id(*merged) &&
                  set.find_by_local_id(*merged)->area(set.edge_cells()) == 65536,
              "joined parcel spans the whole region again");
    }

    // Join must refuse parcels with different owners.
    {
        ParcelSet set(256, "11111111-1111-4111-8111-111111111111",
                      "22222222-2222-4222-8222-222222222222", 1000);
        set.divide(0.0F, 0.0F, 64.0F, 64.0F, "33333333-3333-4333-8333-333333333333",
                   "44444444-4444-4444-8444-444444444444", 2000);
        const auto merged = set.join(0.0F, 0.0F, 256.0F, 256.0F,
                                     "22222222-2222-4222-8222-222222222222");
        check(!merged.has_value(), "join across different owners is rejected");
        check(set.parcels().size() == 2, "parcels unchanged after rejected join");
    }

    if (failures != 0) {
        std::printf("%d parcel test check(s) failed\n", failures);
        return 1;
    }
    std::printf("all parcel tests passed\n");
    return 0;
}
