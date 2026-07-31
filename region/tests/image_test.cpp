#include "homeworldz/image.h"

#include <algorithm>
#include <array>
#include <cmath>
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
    // Small images stay lossless: a compression ratio aims at a byte count, and
    // below a floor that count destroys the image to save nothing. This 32x24
    // is under the floor, so it must come back bit-exact.
    if (decoded->pixels != src.pixels) {
        std::cerr << "small-image round-trip did not reproduce pixels exactly\n";
        return 1;
    }

    // A four-channel image round-trips with its alpha intact. This is the
    // path a GLB texture takes (ADR 0033 M3): PNGs are commonly RGBA even
    // when fully opaque, and an encoder that dropped or corrupted the fourth
    // component would turn an opaque face transparent in a viewer while every
    // byte-level check upstream still passed.
    {
        Image rgba;
        rgba.width = 8;
        rgba.height = 8;
        rgba.channels = 4;
        rgba.pixels.resize(rgba.expected_size());
        for (std::uint32_t y = 0; y < rgba.height; ++y)
            for (std::uint32_t x = 0; x < rgba.width; ++x) {
                const auto index = (static_cast<std::size_t>(y) * rgba.width + x) * 4;
                const bool red = ((x + y) % 2) == 0;
                rgba.pixels[index] = 255;
                rgba.pixels[index + 1] = red ? 0 : 255;
                rgba.pixels[index + 2] = red ? 0 : 255;
                rgba.pixels[index + 3] = 255;  // fully opaque throughout
            }
        const auto coded = encode_j2c(rgba);
        if (!coded || coded->empty()) {
            std::cerr << "encode_j2c refused a four-channel image\n";
            return 1;
        }
        const auto back = decode_j2c(*coded);
        if (!back) {
            std::cerr << "decode_j2c refused its own four-channel output\n";
            return 1;
        }
        if (back->channels != 4) {
            std::cerr << "four-channel round-trip lost a component: "
                      << static_cast<int>(back->channels) << '\n';
            return 1;
        }
        for (std::size_t pixel = 0; pixel < back->pixel_count(); ++pixel)
            if (back->pixels[pixel * 4 + 3] != 255) {
                std::cerr << "alpha did not survive the round-trip at pixel " << pixel
                          << ": " << static_cast<int>(back->pixels[pixel * 4 + 3]) << '\n';
                return 1;
            }
    }

    // Above the small-image floor the encode is lossy, because a viewer's
    // JPEG2000 is a derived form and lossless made a 1024 terrain layer 2 MB
    // (measured 2026-07-31). Two things have to hold together, and neither
    // alone is worth anything: it must actually compress, and what comes back
    // must still be the picture. So assert the size *and* the peak
    // signal-to-noise ratio, on a photographic-ish gradient rather than a
    // synthetic checkerboard, since hard edges are the one thing a wavelet
    // coder flatters least and terrain is not made of them.
    {
        Image big;
        big.width = 256;
        big.height = 256;
        big.channels = 3;
        big.pixels.resize(big.expected_size());
        for (std::uint32_t y = 0; y < big.height; ++y)
            for (std::uint32_t x = 0; x < big.width; ++x) {
                const auto index = (static_cast<std::size_t>(y) * big.width + x) * 3;
                // Smooth ramps plus a low-amplitude ripple: continuous tone
                // with real local variation, which is what a ground texture is.
                const auto ripple = static_cast<int>(12.0 * std::sin(x * 0.15) * std::cos(y * 0.11));
                big.pixels[index + 0] = static_cast<std::uint8_t>(
                    std::clamp(static_cast<int>(x) / 2 + 40 + ripple, 0, 255));
                big.pixels[index + 1] = static_cast<std::uint8_t>(
                    std::clamp(static_cast<int>(y) / 2 + 60 - ripple, 0, 255));
                big.pixels[index + 2] = static_cast<std::uint8_t>(
                    std::clamp(static_cast<int>(x + y) / 4 + 30 + ripple, 0, 255));
            }
        const auto coded = encode_j2c(big);
        if (!coded || coded->empty()) {
            std::cerr << "encode_j2c refused a 256x256 image\n";
            return 1;
        }
        // 20:1 against the raw samples, with slack for header and rounding.
        const auto raw = big.pixels.size();
        if (coded->size() > raw / 10) {
            std::cerr << "lossy encode did not compress: " << coded->size() << " bytes from "
                      << raw << " raw (expected near " << raw / 20 << ")\n";
            return 1;
        }
        const auto back = decode_j2c(*coded);
        if (!back || back->pixels.size() != big.pixels.size()) {
            std::cerr << "lossy round-trip did not return the same shape\n";
            return 1;
        }
        double squared = 0.0;
        for (std::size_t i = 0; i < big.pixels.size(); ++i) {
            const double difference =
                static_cast<int>(back->pixels[i]) - static_cast<int>(big.pixels[i]);
            squared += difference * difference;
        }
        const auto mean_squared = squared / static_cast<double>(big.pixels.size());
        const auto psnr = mean_squared > 0.0 ? 10.0 * std::log10(255.0 * 255.0 / mean_squared) : 99.0;
        // 40 dB is the conventional threshold for visually lossless
        // photographic content; measured 2026-07-31 at well above it.
        if (psnr < 40.0) {
            std::cerr << "lossy round-trip lost too much: PSNR " << psnr << " dB\n";
            return 1;
        }
        std::cerr << "image j2c lossy encode OK (" << raw << " raw -> " << coded->size()
                  << " bytes, PSNR " << psnr << " dB)\n";
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
