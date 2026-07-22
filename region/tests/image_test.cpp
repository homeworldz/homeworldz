#include "homeworldz/image.h"

#include <cstdint>
#include <iostream>
#include <vector>

using homeworldz::image::decode_j2c;
using homeworldz::image::encode_j2c;
using homeworldz::image::Image;

namespace {

// A small RGBA gradient so a lossless round-trip has real per-channel content
// to reproduce exactly.
Image make_gradient(std::uint32_t w, std::uint32_t h) {
    Image img;
    img.width = w;
    img.height = h;
    img.channels = 4;
    img.pixels.resize(img.expected_size());
    for (std::uint32_t y = 0; y < h; ++y) {
        for (std::uint32_t x = 0; x < w; ++x) {
            std::size_t i = (static_cast<std::size_t>(y) * w + x) * 4;
            img.pixels[i + 0] = static_cast<std::uint8_t>(x * 255 / (w - 1));
            img.pixels[i + 1] = static_cast<std::uint8_t>(y * 255 / (h - 1));
            img.pixels[i + 2] = static_cast<std::uint8_t>((x + y) & 0xFF);
            img.pixels[i + 3] = 255;
        }
    }
    return img;
}

}  // namespace

int main() {
    const Image src = make_gradient(32, 24);

    auto encoded = encode_j2c(src);
    if (!encoded || encoded->empty()) {
        std::cerr << "encode_j2c failed\n";
        return 1;
    }

    auto decoded = decode_j2c(*encoded);
    if (!decoded) {
        std::cerr << "decode_j2c failed\n";
        return 1;
    }
    if (decoded->width != src.width || decoded->height != src.height ||
        decoded->channels != src.channels) {
        std::cerr << "dimensions changed across round-trip: " << decoded->width << "x"
                  << decoded->height << "x" << static_cast<int>(decoded->channels) << '\n';
        return 1;
    }
    if (decoded->pixels != src.pixels) {
        std::cerr << "lossless round-trip did not reproduce pixels exactly\n";
        return 1;
    }

    // Garbage input must be rejected, not crash.
    if (decode_j2c(std::vector<std::uint8_t>{0x00, 0x01, 0x02, 0x03, 0x04}).has_value()) {
        std::cerr << "decode_j2c accepted non-JPEG2000 input\n";
        return 1;
    }

    std::cerr << "image j2c lossless round-trip OK (" << encoded->size() << " bytes)\n";
    return 0;
}
