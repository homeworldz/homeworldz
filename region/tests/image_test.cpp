#include "homeworldz/image.h"

#include <array>
#include <cstdint>
#include <iostream>
#include <vector>

using homeworldz::image::composite_rgba;
using homeworldz::image::decode_j2c;
using homeworldz::image::encode_j2c;
using homeworldz::image::Image;
using homeworldz::image::Layer;
using homeworldz::image::resize_nearest;
using homeworldz::image::to_rgba;

namespace {

// A solid width x height image with the given channels, every pixel = value.
Image make_solid(std::uint32_t w, std::uint32_t h, std::uint8_t channels,
                 std::array<std::uint8_t, 4> value) {
    Image img;
    img.width = w;
    img.height = h;
    img.channels = channels;
    img.pixels.resize(img.expected_size());
    for (std::size_t i = 0; i < img.pixel_count(); ++i)
        for (std::uint8_t c = 0; c < channels; ++c) img.pixels[i * channels + c] = value[c];
    return img;
}

bool near(int a, int b, int tol = 1) { return (a - b <= tol) && (b - a <= tol); }


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

    // to_rgba expands RGB to opaque RGBA.
    {
        Image rgb = make_solid(2, 2, 3, {10, 20, 30, 0});
        Image rgba = to_rgba(rgb);
        if (rgba.channels != 4 || rgba.pixels[0] != 10 || rgba.pixels[1] != 20 ||
            rgba.pixels[2] != 30 || rgba.pixels[3] != 255) {
            std::cerr << "to_rgba(RGB) did not expand to opaque RGBA\n";
            return 1;
        }
    }

    // resize_nearest doubles dimensions and preserves the solid color.
    {
        Image src = make_solid(2, 2, 4, {5, 6, 7, 8});
        Image big = resize_nearest(src, 4, 4);
        if (big.width != 4 || big.height != 4 || big.pixels.size() != 4 * 4 * 4 ||
            big.pixels[0] != 5 || big.pixels[3] != 8) {
            std::cerr << "resize_nearest failed\n";
            return 1;
        }
    }

    // Tint multiplies the source color: white tinted red -> red.
    {
        std::vector<Layer> layers{{make_solid(1, 1, 4, {255, 255, 255, 255}), {255, 0, 0}}};
        Image out = composite_rgba(1, 1, layers);
        if (!near(out.pixels[0], 255) || !near(out.pixels[1], 0) ||
            !near(out.pixels[2], 0) || out.pixels[3] != 255) {
            std::cerr << "tinted composite wrong: " << int(out.pixels[0]) << ','
                      << int(out.pixels[1]) << ',' << int(out.pixels[2]) << '\n';
            return 1;
        }
    }

    // Source-over: 50% blue over opaque red -> ~(127,0,128,255).
    {
        std::vector<Layer> layers{
            {make_solid(1, 1, 4, {255, 0, 0, 255}), {255, 255, 255}},
            {make_solid(1, 1, 4, {0, 0, 255, 128}), {255, 255, 255}},
        };
        Image out = composite_rgba(1, 1, layers);
        if (!near(out.pixels[0], 127, 2) || !near(out.pixels[1], 0) ||
            !near(out.pixels[2], 128, 2) || out.pixels[3] != 255) {
            std::cerr << "alpha blend wrong: " << int(out.pixels[0]) << ','
                      << int(out.pixels[1]) << ',' << int(out.pixels[2]) << ','
                      << int(out.pixels[3]) << '\n';
            return 1;
        }
    }

    std::cerr << "image j2c lossless round-trip OK (" << encoded->size() << " bytes)\n";
    std::cerr << "image composite/resize/tint OK\n";
    return 0;
}
