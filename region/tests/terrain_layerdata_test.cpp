// What a viewer actually receives as ground.
//
// The client core reported (2026-07-30) that terrain looks rougher in
// Firestorm than in the session client rendering the same region, and offered
// a candidate rather than a finding: LayerData is lossy, so a viewer may be
// looking at compression noise while a client looks at the terrain. The check
// they proposed is the one used on the mesh renditions -- encode, decode,
// compare against the source, and assert the counts are non-zero so a test
// that silently compared nothing cannot pass.
//
// So this decodes encode_terrain's own wire bytes back to heights, with a
// decoder written here from the format rather than shared with the encoder:
// a round-trip through one body of code proves only that it is
// self-consistent. Bounds below are measured, not assumed, and are stated per
// terrain shape because the format's error is not uniform -- it scales with
// the relief inside a 16x16 patch, which is the whole point of the finding.
//
// The answer, measured: the encoder is faithful. A 50 m rise inside one patch
// comes back within 0.12 m, and a deliberate one-metre-pitch +/-4 m
// checkerboard -- far rougher than any brush leaves -- within 0.36 m. That is
// well under the relief a viewer's terrain visibly shows, so LayerData
// quantization is not what makes Firestorm's ground look rougher than the
// client's. Whatever the difference is, both are drawing the same heights.
#include "homeworldz/viewer_protocol.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <span>
#include <vector>

namespace {

using homeworldz::viewer::TerrainPatch;

// MSB-first within each byte, the order BitWriter emits.
class BitReader {
public:
    explicit BitReader(std::span<const std::byte> data) : data_(data) {}

    std::uint32_t read(unsigned bits) {
        std::uint32_t value = 0;
        for (unsigned index = 0; index < bits; ++index) {
            const auto byte = position_ / 8;
            const auto bit = 7 - (position_ % 8);
            const std::uint32_t set = byte < data_.size()
                ? (std::to_integer<std::uint32_t>(data_[byte]) >> bit) & 1U : 0U;
            value = (value << 1) | set;
            ++position_;
            if (byte >= data_.size()) overran_ = true;
        }
        return value;
    }

    // The inverse of write_ll_bits: whole bytes low-order first, then any
    // remaining bits as the high part.
    std::uint32_t read_ll(unsigned count) {
        std::uint32_t value = 0;
        unsigned shift = 0;
        while (count >= 8) {
            value |= read(8) << shift;
            shift += 8;
            count -= 8;
        }
        if (count != 0) value |= read(count) << shift;
        return value;
    }

    bool overran() const { return overran_; }

private:
    std::span<const std::byte> data_;
    std::size_t position_{};
    bool overran_{};
};

const std::array<float, 256>& cosines() {
    static const auto table = [] {
        std::array<float, 256> result{};
        constexpr float pi = 3.14159265358979323846F;
        for (int u = 0; u < 16; ++u)
            for (int n = 0; n < 16; ++n)
                result[u * 16 + n] = std::cos((2.0F * n + 1.0F) * u * pi * 0.5F / 16.0F);
        return result;
    }();
    return table;
}

const std::array<int, 256>& copy_matrix() {
    static const auto table = [] {
        std::array<int, 256> result{};
        bool diagonal = false;
        bool right = true;
        int x = 0;
        int y = 0;
        int count = 0;
        while (x < 16 && y < 16) {
            result[y * 16 + x] = count++;
            if (!diagonal) {
                if (right) {
                    if (x < 15) ++x; else ++y;
                    right = false;
                } else {
                    if (y < 15) ++y; else ++x;
                    right = true;
                }
                diagonal = true;
            } else if (right) {
                ++x;
                --y;
                if (x == 15 || y == 0) diagonal = false;
            } else {
                --x;
                ++y;
                if (y == 15 || x == 0) diagonal = false;
            }
        }
        return result;
    }();
    return table;
}

struct DecodedPatch {
    std::uint32_t patch_id{};
    std::array<float, 256> heights{};
};

// One patch's worth of heights, reconstructed the way a viewer reconstructs
// them: undo the zig-zag and the frequency quantizer, inverse DCT, then map
// the 0..1024 domain back through the patch's own range and DC offset.
std::vector<DecodedPatch> decode_layer(std::span<const std::byte> message, bool extended,
                                       std::string& error) {
    std::vector<DecodedPatch> patches;
    if (message.size() < 8) {
        error = "message too short to be a LayerData";
        return patches;
    }
    // {11, layer type, size low, size high} then the four stride/size bytes.
    const auto body = message.subspan(4);
    BitReader bits(body);
    if (bits.read(8) != 0x08 || bits.read(8) != 0x01 || bits.read(8) != 16) {
        error = "layer group header is not the stride/patch-size preamble";
        return patches;
    }
    bits.read(8);  // layer type, already known from the message

    constexpr float inverse_sqrt_two = 0.7071067811865475244F;
    const auto& cosine = cosines();
    const auto& copy = copy_matrix();
    while (true) {
        const auto quant_wbits = bits.read(8);
        if (quant_wbits == 97) break;  // end of patches
        if (bits.overran()) {
            error = "ran off the end of the patch stream";
            return patches;
        }
        const auto word_bits = (quant_wbits & 0x0f) + 2;
        const auto offset_bits = bits.read_ll(32);
        float dc_offset{};
        std::memcpy(&dc_offset, &offset_bits, sizeof(dc_offset));
        const auto range = static_cast<int>(bits.read_ll(16));
        const auto patch_id = bits.read_ll(extended ? 32 : 10);

        std::array<int, 256> coefficients{};
        for (std::size_t index = 0; index < coefficients.size(); ++index) {
            if (bits.read(1) == 0) continue;              // 0        -> zero
            if (bits.read(1) == 0) break;                 // 10       -> rest zero
            const bool negative = bits.read(1) != 0;      // 110/111  -> value
            const auto magnitude = static_cast<int>(bits.read_ll(word_bits));
            coefficients[index] = negative ? -magnitude : magnitude;
        }

        // The frequency quantizer the encoder divided by, undone.
        std::array<float, 256> block{};
        for (int j = 0; j < 16; ++j)
            for (int i = 0; i < 16; ++i)
                block[j * 16 + i] = static_cast<float>(coefficients[copy[j * 16 + i]]) *
                                    (1.0F + 2.0F * static_cast<float>(i + j));

        std::array<float, 256> intermediate{};
        for (int column = 0; column < 16; ++column) {
            for (int n = 0; n < 16; ++n) {
                float total = inverse_sqrt_two * block[column];
                for (int u = 1; u < 16; ++u)
                    total += block[16 * u + column] * cosine[u * 16 + n];
                intermediate[16 * n + column] = total;
            }
        }
        std::array<float, 256> values{};
        for (int line = 0; line < 16; ++line) {
            for (int n = 0; n < 16; ++n) {
                float total = inverse_sqrt_two * intermediate[line * 16];
                for (int u = 1; u < 16; ++u)
                    total += intermediate[line * 16 + u] * cosine[u * 16 + n];
                values[line * 16 + n] = total * (2.0F / 16.0F);
            }
        }

        const auto multiply = static_cast<float>(range) / 1024.0F;
        const auto add = multiply * 512.0F + dc_offset;
        DecodedPatch decoded;
        decoded.patch_id = patch_id;
        for (std::size_t index = 0; index < values.size(); ++index)
            decoded.heights[index] = values[index] * multiply + add;
        patches.push_back(decoded);
    }
    return patches;
}

constexpr std::size_t width = 256;

struct Shape {
    const char* name;
    std::uint8_t patch_x;
    std::uint8_t patch_y;
    float tolerance;  // metres; measured, then given headroom
};

float height_at(const Shape& shape, std::size_t x, std::size_t y) {
    const auto u = static_cast<float>(x % 16);
    const auto v = static_cast<float>(y % 16);
    const std::string_view name = shape.name;
    if (name == "flat") return 21.0F;
    // A metre of rise per metre east: the ordinary ground most of a region is.
    if (name == "gentle") return 22.0F + u * 0.25F;
    // The steep face the operator built on Gamma to test walkable slope: about
    // 50 m of rise inside one 16 m patch.
    if (name == "steep") return 23.0F + v * 3.1F;
    // Adjacent vertices metres apart in height: what a raise brush leaves
    // behind, and the worst case for a frequency-domain coder.
    return 30.0F + ((static_cast<int>(u) + static_cast<int>(v)) % 2 == 0 ? 4.0F : -4.0F);
}

}  // namespace

int main() {
    // The viewer's composition arithmetic, which decides which layer a height
    // shows. Asserted because the semantics were published wrong twice: first as a
    // start and a span (right), then retracted to two absolute bounds on the
    // strength of the viewer's dialog text (wrong), then restored when an operator
    // photographed sand at 22 m under start 20 / range 60. t there is 0.13 - layer
    // 1 almost pure - so the numbers were behaving exactly as the renderer says.
    {
        using homeworldz::terrain::layer_composition_value;
        // The case from the screenshot: layer 1 still dominant well above 20.
        if (std::abs(layer_composition_value(22.0F, 20.0F, 60.0F) - 0.13333F) > 1e-4F) return 60;
        // Boundaries sit at half-integer t. With start 10 and range 80 they land on
        // 20 / 40 / 60, which is what an operator asking for those wants.
        if (std::abs(layer_composition_value(20.0F, 10.0F, 80.0F) - 0.5F) > 1e-5F) return 61;
        if (std::abs(layer_composition_value(40.0F, 10.0F, 80.0F) - 1.5F) > 1e-5F) return 62;
        if (std::abs(layer_composition_value(60.0F, 10.0F, 80.0F) - 2.5F) > 1e-5F) return 63;
        // Clamped at both ends, so ground below the start or above the top of the
        // span is the first or last layer rather than an extrapolation.
        if (layer_composition_value(-100.0F, 10.0F, 80.0F) != 0.0F) return 64;
        if (layer_composition_value(1000.0F, 10.0F, 80.0F) != 3.0F) return 65;
        // Layer 4 becomes pure at start + 0.75 * range, not at the second number.
        if (layer_composition_value(70.0F, 10.0F, 80.0F) != 3.0F) return 66;
        // A zero range would divide by zero in a viewer; guarded rather than
        // propagated as an infinity.
        if (layer_composition_value(50.0F, 10.0F, 0.0F) != 0.0F) return 67;
    }

    const std::array<Shape, 4> shapes{{
        // Measured worst deviations, 2026-07-31: 0.00006 m, 0.011 m, 0.116 m,
        // 0.359 m. Bounds are those with headroom, so a real regression trips
        // them and float differences between compilers do not.
        {"flat", 0, 0, 0.001F},
        {"gentle", 1, 0, 0.03F},
        {"steep", 2, 0, 0.25F},
        {"spiky", 3, 0, 0.75F},
    }};

    std::vector<float> heightmap(width * width, 20.0F);
    for (const auto& shape : shapes)
        for (std::size_t y = 0; y < 16; ++y)
            for (std::size_t x = 0; x < 16; ++x) {
                const auto sample_x = shape.patch_x * 16 + x;
                const auto sample_y = shape.patch_y * 16 + y;
                heightmap[sample_y * width + sample_x] = height_at(shape, sample_x, sample_y);
            }

    std::vector<TerrainPatch> patches;
    for (const auto& shape : shapes) patches.push_back(TerrainPatch{shape.patch_x, shape.patch_y});

    const auto message = homeworldz::viewer::encode_terrain(patches, heightmap);
    if (message.empty()) {
        std::cerr << "encode_terrain refused a 256x256 heightmap\n";
        return 1;
    }

    std::string error;
    const auto decoded = decode_layer(message, false, error);
    if (!error.empty()) {
        std::cerr << "decode failed: " << error << '\n';
        return 2;
    }
    // The non-zero counts: a decoder that found no patches would otherwise
    // pass every comparison below by comparing nothing.
    if (decoded.size() != shapes.size()) {
        std::cerr << "decoded " << decoded.size() << " patches, encoded " << shapes.size() << '\n';
        return 3;
    }

    std::size_t compared = 0;
    for (std::size_t index = 0; index < shapes.size(); ++index) {
        const auto& shape = shapes[index];
        const auto expected_id = static_cast<std::uint32_t>(shape.patch_x) << 5 | shape.patch_y;
        if (decoded[index].patch_id != expected_id) {
            std::cerr << shape.name << ": patch id " << decoded[index].patch_id << ", expected "
                      << expected_id << '\n';
            return 4;
        }
        float worst = 0.0F;
        double squared = 0.0;
        for (std::size_t y = 0; y < 16; ++y)
            for (std::size_t x = 0; x < 16; ++x) {
                const auto source = heightmap[(shape.patch_y * 16 + y) * width +
                                              shape.patch_x * 16 + x];
                const auto back = decoded[index].heights[y * 16 + x];
                const auto deviation = std::fabs(back - source);
                worst = std::max(worst, deviation);
                squared += static_cast<double>(deviation) * deviation;
                ++compared;
            }
        const auto rms = std::sqrt(squared / 256.0);
        std::cerr << "  " << shape.name << ": worst " << worst << " m, rms " << rms
                  << " m (tolerance " << shape.tolerance << " m)\n";
        if (worst > shape.tolerance) {
            std::cerr << shape.name << ": LayerData round-trip deviates more than the measured"
                         " bound; the encoder changed or the terrain shape did\n";
            return 5;
        }
    }
    if (compared != shapes.size() * 256) {
        std::cerr << "compared " << compared << " samples, expected " << shapes.size() * 256 << '\n';
        return 6;
    }

    std::cerr << "terrain LayerData round-trip OK (" << compared << " samples across "
              << decoded.size() << " patches, " << message.size() << " wire bytes)\n";
    return 0;
}
