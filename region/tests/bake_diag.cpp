// Offline diagnostic: reproduce the server default-outfit bake against the
// bundled default-avatar assets and report, for each baked slot, the image
// dimensions and per-channel stats (including alpha), plus the resulting
// texture-entry face -> UUID mapping. Not a unit test; run manually:
//   homeworldz-bake-diag <path-to-assets/region/default-avatar>
#include "homeworldz/appearance_bake.h"
#include "homeworldz/bake.h"
#include "homeworldz/image.h"
#include "homeworldz/viewer_protocol.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

using homeworldz::image::decode_j2c;
using homeworldz::image::Image;
using homeworldz::viewer::bake_worn_outfit;
using homeworldz::viewer::format_uuid;
using homeworldz::viewer::parse_uuid;
using homeworldz::viewer::unpack_texture_entry_faces;
using homeworldz::viewer::Uuid;

namespace fs = std::filesystem;

namespace {

std::optional<std::vector<std::byte>> read_file(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return std::nullopt;
    std::vector<char> raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::vector<std::byte> out(raw.size());
    if (!raw.empty()) std::memcpy(out.data(), raw.data(), raw.size());
    return out;
}

void report_image(const char* label, const Image& img) {
    if (img.empty()) {
        std::cout << label << ": <empty/undecodable>\n";
        return;
    }
    long long sum[4] = {0, 0, 0, 0};
    int amin = 255, amax = 0;
    const std::size_t n = img.pixel_count();
    for (std::size_t i = 0; i < n; ++i) {
        for (std::uint8_t c = 0; c < img.channels; ++c)
            sum[c] += img.pixels[i * img.channels + c];
        if (img.channels == 4) {
            int a = img.pixels[i * 4 + 3];
            if (a < amin) amin = a;
            if (a > amax) amax = a;
        }
    }
    std::cout << label << ": " << img.width << "x" << img.height << " ch=" << int(img.channels);
    std::cout << " avg=";
    for (std::uint8_t c = 0; c < img.channels; ++c)
        std::cout << (c ? "," : "") << (n ? sum[c] / (long long)n : 0);
    if (img.channels == 4) std::cout << " alpha[min=" << amin << ",max=" << amax << "]";
    std::cout << "\n";
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: homeworldz-bake-diag <assets/region/default-avatar dir>\n";
        return 2;
    }
    const fs::path dir = argv[1];

    auto fetch = [&](const Uuid& id) -> std::optional<std::vector<std::byte>> {
        const std::string u = format_uuid(id);
        for (const char* ext : {".j2c", ".bodypart", ".clothing", ".tga", ""}) {
            auto bytes = read_file(dir / (u + ext));
            if (bytes) return bytes;
        }
        std::cout << "  fetch MISS: " << u << "\n";
        return std::nullopt;
    };

    // Inspect the skin body textures directly first.
    std::cout << "=== source skin textures ===\n";
    for (const char* t : {"00000000-0000-1111-9999-000000000012",   // head skin
                          "00000000-0000-1111-9999-000000000010",   // upper skin
                          "00000000-0000-1111-9999-000000000011"}) {  // lower skin
        auto id = parse_uuid(t);
        auto bytes = fetch(*id);
        if (!bytes) continue;
        std::vector<std::uint8_t> raw(bytes->size());
        std::memcpy(raw.data(), bytes->data(), bytes->size());
        auto img = decode_j2c(raw);
        report_image(t, img ? *img : Image{});
    }

    const std::vector<std::string> ids = {
        "66c41e39-38f9-f75a-024e-585989bfab73", "77c41e39-38f9-f75a-024e-585989bbabbb",
        "d342e6c0-b9d2-11dc-95ff-0800200c9a66", "4bb6fa4d-1cd2-498a-a84c-95c1a0e745a7",
        "00000000-38f9-1111-024e-222222111110", "00000000-38f9-1111-024e-222222111120"};
    std::vector<Uuid> wearable_ids;
    for (const auto& s : ids)
        if (auto u = parse_uuid(s)) wearable_ids.push_back(*u);

    const Uuid default_id = *parse_uuid("3a367d1c-bef1-6d43-7595-e88c1e3aadb3");

    std::cout << "\n=== bake ===\n";
    auto bake = bake_worn_outfit(wearable_ids, default_id, fetch);
    if (!bake) {
        std::cout << "bake_worn_outfit returned nullopt\n";
        return 1;
    }
    std::cout << "parsed wearables: " << bake->worn.size() << ", baked assets: " << bake->assets.size()
              << "\n";
    for (const auto& asset : bake->assets) {
        std::vector<std::uint8_t> raw(asset.j2c.size());
        std::memcpy(raw.data(), asset.j2c.data(), asset.j2c.size());
        auto img = decode_j2c(raw);
        std::string label = "  TE index " + std::to_string(asset.texture_index) + " (" +
                            std::to_string(asset.j2c.size()) + " bytes j2c)";
        report_image(label.c_str(), img ? *img : Image{});
    }

    std::cout << "\n=== texture_entry faces (baked slots) ===\n";
    auto faces = unpack_texture_entry_faces(bake->texture_entry);
    if (!faces) {
        std::cout << "texture_entry failed to decode!\n";
        return 1;
    }
    for (int idx : {8, 9, 10, 11, 19, 20}) {
        const auto& id = (*faces)[idx];
        std::cout << "  face " << idx << " = " << format_uuid(id)
                  << (id == default_id ? "  (default/invisible)" : "") << "\n";
    }
    return 0;
}
