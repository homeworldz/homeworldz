#ifndef HOMEWORLDZ_IMAGE_H
#define HOMEWORLDZ_IMAGE_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

// Raster image + JPEG2000 codec wrapper for server-side appearance baking
// (ADR 0029). The codec is isolated here so the underlying library (OpenJPEG
// today) is a swappable implementation detail; the bake compositor works only
// on the RGBA Image type below.
namespace homeworldz::image {

// A decoded raster: 1..4 8-bit channels (1=L, 2=LA, 3=RGB, 4=RGBA), row-major,
// tightly packed (no row padding). pixels.size() == width*height*channels.
struct Image {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint8_t channels = 0;
    std::vector<std::uint8_t> pixels;

    bool empty() const { return width == 0 || height == 0 || channels == 0; }
    std::size_t pixel_count() const {
        return static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    }
    std::size_t expected_size() const { return pixel_count() * channels; }
};

// Decode a JPEG2000 blob (raw J2K codestream, e.g. Second Life .j2c, or a JP2
// wrapper) into 8-bit channels. Returns nullopt on malformed input, an
// unsupported layout (subsampled/mismatched components), or if no codec is
// compiled in.
std::optional<Image> decode_j2c(const std::vector<std::uint8_t>& data);

// Encode an Image as a lossless JPEG2000 codestream (.j2c). Returns nullopt on
// invalid input or encoder failure.
std::optional<std::vector<std::uint8_t>> encode_j2c(const Image& image);

}  // namespace homeworldz::image

#endif  // HOMEWORLDZ_IMAGE_H
