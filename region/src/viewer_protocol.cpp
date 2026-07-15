#include "homeworldz/viewer_protocol.h"

#include <algorithm>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstring>
#include <limits>

namespace homeworldz::viewer {
namespace {

constexpr std::array<std::byte, 4> use_circuit_code_id{
    std::byte{0xff}, std::byte{0xff}, std::byte{0x00}, std::byte{0x03}};
constexpr std::array<std::byte, 4> packet_ack_id{
    std::byte{0xff}, std::byte{0xff}, std::byte{0xff}, std::byte{0xfb}};
constexpr std::array<std::byte, 4> region_handshake_id{
    std::byte{0xff}, std::byte{0xff}, std::byte{0x00}, std::byte{0x94}};
constexpr std::array<std::byte, 4> region_handshake_reply_id{
    std::byte{0xff}, std::byte{0xff}, std::byte{0x00}, std::byte{0x95}};
constexpr std::array<std::byte, 4> complete_agent_movement_id{
    std::byte{0xff}, std::byte{0xff}, std::byte{0x00}, std::byte{0xf9}};
constexpr std::array<std::byte, 4> agent_movement_complete_id{
    std::byte{0xff}, std::byte{0xff}, std::byte{0x00}, std::byte{0xfa}};
constexpr std::array<std::byte, 4> chat_from_viewer_id{
    std::byte{0xff}, std::byte{0xff}, std::byte{0x00}, std::byte{0x50}};
constexpr std::array<std::byte, 4> chat_from_simulator_id{
    std::byte{0xff}, std::byte{0xff}, std::byte{0x00}, std::byte{0x8b}};
constexpr std::array<std::byte, 4> logout_request_id{
    std::byte{0xff}, std::byte{0xff}, std::byte{0x00}, std::byte{0xfc}};
constexpr std::array<std::byte, 4> logout_reply_id{
    std::byte{0xff}, std::byte{0xff}, std::byte{0x00}, std::byte{0xfd}};
constexpr std::array<std::byte, 4> agent_cached_texture_id{
    std::byte{0xff}, std::byte{0xff}, std::byte{0x01}, std::byte{0x80}};
constexpr std::array<std::byte, 4> agent_cached_texture_response_id{
    std::byte{0xff}, std::byte{0xff}, std::byte{0x01}, std::byte{0x81}};
constexpr std::array<std::byte, 4> agent_set_appearance_id{
    std::byte{0xff}, std::byte{0xff}, std::byte{0x00}, std::byte{0x54}};
constexpr std::array<std::byte, 4> create_inventory_folder_id{
    std::byte{0xff}, std::byte{0xff}, std::byte{0x01}, std::byte{0x11}};
constexpr std::array<std::byte, 4> copy_inventory_item_id{
    std::byte{0xff}, std::byte{0xff}, std::byte{0x01}, std::byte{0x0d}};
constexpr std::array<std::byte, 4> update_create_inventory_item_id{
    std::byte{0xff}, std::byte{0xff}, std::byte{0x01}, std::byte{0x0b}};
constexpr std::array<std::byte, 4> move_inventory_folder_id{
    std::byte{0xff}, std::byte{0xff}, std::byte{0x01}, std::byte{0x13}};
constexpr std::array<std::byte, 4> move_inventory_item_id{
    std::byte{0xff}, std::byte{0xff}, std::byte{0x01}, std::byte{0x0c}};
constexpr std::array<std::byte, 2> object_add_id{std::byte{0xff}, std::byte{0x01}};
constexpr std::array<std::byte, 4> derez_object_id{
    std::byte{0xff}, std::byte{0xff}, std::byte{0x01}, std::byte{0x23}};
constexpr std::array<std::byte, 4> rez_object_id{
    std::byte{0xff}, std::byte{0xff}, std::byte{0x01}, std::byte{0x25}};
constexpr std::array<std::byte, 4> object_select_id{
    std::byte{0xff}, std::byte{0xff}, std::byte{0x00}, std::byte{0x6e}};
constexpr std::array<std::byte, 2> multiple_object_update_id{
    std::byte{0xff}, std::byte{0x02}};
constexpr std::array<std::byte, 4> object_name_id{
    std::byte{0xff}, std::byte{0xff}, std::byte{0x00}, std::byte{0x6b}};
constexpr std::array<std::byte, 4> object_description_id{
    std::byte{0xff}, std::byte{0xff}, std::byte{0x00}, std::byte{0x6c}};
constexpr std::array<std::byte, 4> object_permissions_id{
    std::byte{0xff}, std::byte{0xff}, std::byte{0x00}, std::byte{0x69}};
constexpr std::array<std::byte, 4> object_duplicate_id{
    std::byte{0xff}, std::byte{0xff}, std::byte{0x00}, std::byte{0x5a}};
constexpr std::array<std::byte, 4> object_material_id{
    std::byte{0xff}, std::byte{0xff}, std::byte{0x00}, std::byte{0x61}};
constexpr std::array<std::byte, 2> request_object_properties_family_id{
    std::byte{0xff}, std::byte{0x05}};
constexpr std::array<std::byte, 4> uuid_name_request_id{
    std::byte{0xff}, std::byte{0xff}, std::byte{0x00}, std::byte{0xeb}};
constexpr std::array<std::byte, 4> uuid_name_reply_id{
    std::byte{0xff}, std::byte{0xff}, std::byte{0x00}, std::byte{0xec}};
constexpr std::array<std::byte, 4> economy_data_request_id{
    std::byte{0xff}, std::byte{0xff}, std::byte{0x00}, std::byte{0x18}};
constexpr std::array<std::byte, 4> economy_data_id{
    std::byte{0xff}, std::byte{0xff}, std::byte{0x00}, std::byte{0x19}};

class BitWriter {
public:
    void write(std::uint32_t value, unsigned bits) {
        for (unsigned remaining = bits; remaining > 0; --remaining) {
            current_ = static_cast<std::uint8_t>((current_ << 1) | ((value >> (remaining - 1)) & 1));
            if (++used_ == 8) flush_byte();
        }
    }
    void write_byte(std::uint8_t value) { write(value, 8); }
    std::vector<std::byte> finish() {
        if (used_ != 0) {
            current_ <<= (8 - used_);
            flush_byte();
        }
        return std::move(bytes_);
    }
private:
    void flush_byte() {
        bytes_.push_back(static_cast<std::byte>(current_));
        current_ = 0;
        used_ = 0;
    }
    std::vector<std::byte> bytes_;
    std::uint8_t current_{};
    unsigned used_{};
};

void write_ll_bits(BitWriter& output, std::uint32_t value, unsigned count) {
    while (count >= 8) {
        output.write_byte(static_cast<std::uint8_t>(value));
        value >>= 8;
        count -= 8;
    }
    if (count != 0) output.write(value, count);
}

struct TerrainPatchHeader {
    float dc_offset{};
    int range{};
    std::uint8_t quant_wbits{136};
    std::uint16_t patch_id{};
};

// Terrain compression follows OpenMetaverse TerrainCompressor's compatible
// DCT, quantization, zig-zag, and bit-packing algorithm. See the retained BSD
// notice in docs/third-party/openmetaverse-terrain.md.

const std::array<float, 256>& terrain_cosines() {
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

const std::array<int, 256>& terrain_copy_matrix() {
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

std::array<int, 256> compress_terrain_patch(std::span<const float> heightmap,
                                            std::uint8_t patch_x, std::uint8_t patch_y,
                                            TerrainPatchHeader& header) {
    float minimum = std::numeric_limits<float>::max();
    float maximum = std::numeric_limits<float>::lowest();
    for (int y = patch_y * 16; y < (patch_y + 1) * 16; ++y) {
        for (int x = patch_x * 16; x < (patch_x + 1) * 16; ++x) {
            const auto value = heightmap[static_cast<std::size_t>(y) * 256 + x];
            minimum = std::min(minimum, value);
            maximum = std::max(maximum, value);
        }
    }
    header.dc_offset = minimum;
    header.range = static_cast<int>(maximum - minimum + 1.0F);
    header.patch_id = static_cast<std::uint16_t>((patch_x << 5) | patch_y);

    const auto premultiply = 1024.0F / static_cast<float>(header.range);
    const auto subtract = 512.0F + header.dc_offset * premultiply;
    std::array<float, 256> block{};
    std::size_t index = 0;
    for (int y = patch_y * 16; y < (patch_y + 1) * 16; ++y)
        for (int x = patch_x * 16; x < (patch_x + 1) * 16; ++x)
            block[index++] = heightmap[static_cast<std::size_t>(y) * 256 + x] * premultiply - subtract;

    constexpr float inverse_sqrt_two = 0.7071067811865475244F;
    const auto& cosine = terrain_cosines();
    std::array<float, 256> intermediate{};
    for (int line = 0; line < 16; ++line) {
        const auto offset = line * 16;
        float total = 0.0F;
        for (int n = 0; n < 16; ++n) total += block[offset + n];
        intermediate[offset] = inverse_sqrt_two * total;
        for (int u = 1; u < 16; ++u) {
            total = 0.0F;
            for (int n = 0; n < 16; ++n)
                total += block[offset + n] * cosine[u * 16 + n];
            intermediate[offset + u] = total;
        }
    }

    const auto& copy = terrain_copy_matrix();
    std::array<int, 256> compressed{};
    for (int column = 0; column < 16; ++column) {
        float total = 0.0F;
        for (int n = 0; n < 16; ++n) total += intermediate[n * 16 + column];
        compressed[copy[column]] = static_cast<int>(inverse_sqrt_two * total * 0.125F /
                                                     (1.0F + 2.0F * column));
        for (int u = 1; u < 16; ++u) {
            total = 0.0F;
            for (int n = 0; n < 16; ++n)
                total += intermediate[n * 16 + column] * cosine[u * 16 + n];
            compressed[copy[u * 16 + column]] = static_cast<int>(total * 0.125F /
                                                                  (1.0F + 2.0F * (u + column)));
        }
    }
    return compressed;
}

unsigned write_terrain_patch_header(BitWriter& output, TerrainPatchHeader header,
                                    const std::array<int, 256>& coefficients) {
    unsigned word_bits = ((header.quant_wbits & 0x0f) + 2) >> 1;
    const auto maximum_bits = static_cast<unsigned>((header.quant_wbits & 0x0f) + 7);
    for (auto value : coefficients) {
        auto magnitude = static_cast<unsigned>(value < 0 ? -static_cast<std::int64_t>(value) : value);
        for (auto bit = maximum_bits; bit > word_bits; --bit) {
            if ((magnitude & (1U << bit)) != 0) {
                word_bits = bit;
                break;
            }
        }
    }
    ++word_bits;
    word_bits = std::clamp(word_bits, 2U, 17U);
    header.quant_wbits = static_cast<std::uint8_t>((header.quant_wbits & 0xf0) | (word_bits - 2));
    output.write_byte(header.quant_wbits);
    std::uint32_t offset_bits{};
    std::memcpy(&offset_bits, &header.dc_offset, sizeof(offset_bits));
    write_ll_bits(output, offset_bits, 32);
    write_ll_bits(output, static_cast<std::uint32_t>(header.range), 16);
    write_ll_bits(output, header.patch_id, 10);
    return word_bits;
}

void write_terrain_coefficients(BitWriter& output, const std::array<int, 256>& coefficients,
                                unsigned word_bits) {
    for (std::size_t index = 0; index < coefficients.size(); ++index) {
        auto value = coefficients[index];
        if (value == 0) {
            if (std::all_of(coefficients.begin() + static_cast<std::ptrdiff_t>(index), coefficients.end(),
                            [](int remaining) { return remaining == 0; })) {
                output.write(2, 2);
                return;
            }
            output.write(0, 1);
            continue;
        }
        output.write(value < 0 ? 7 : 6, 3);
        auto magnitude = static_cast<std::uint32_t>(value < 0 ? -static_cast<std::int64_t>(value) : value);
        magnitude = std::min(magnitude, (1U << word_bits) - 1U);
        write_ll_bits(output, magnitude, word_bits);
    }
}

std::uint32_t read_be_u32(std::span<const std::byte> data, std::size_t offset) {
    return (std::to_integer<std::uint32_t>(data[offset]) << 24) |
           (std::to_integer<std::uint32_t>(data[offset + 1]) << 16) |
           (std::to_integer<std::uint32_t>(data[offset + 2]) << 8) |
           std::to_integer<std::uint32_t>(data[offset + 3]);
}

void append_be_u32(std::vector<std::byte>& output, std::uint32_t value) {
    output.push_back(static_cast<std::byte>((value >> 24) & 0xff));
    output.push_back(static_cast<std::byte>((value >> 16) & 0xff));
    output.push_back(static_cast<std::byte>((value >> 8) & 0xff));
    output.push_back(static_cast<std::byte>(value & 0xff));
}

std::uint32_t read_le_u32(std::span<const std::byte> data, std::size_t offset) {
    return std::to_integer<std::uint32_t>(data[offset]) |
           (std::to_integer<std::uint32_t>(data[offset + 1]) << 8) |
           (std::to_integer<std::uint32_t>(data[offset + 2]) << 16) |
           (std::to_integer<std::uint32_t>(data[offset + 3]) << 24);
}

float read_f32(std::span<const std::byte> data, std::size_t offset) {
    const auto bits = read_le_u32(data, offset);
    float result{};
    std::memcpy(&result, &bits, sizeof(result));
    return result;
}

std::array<float, 3> read_vector3(std::span<const std::byte> data, std::size_t offset) {
    return {read_f32(data, offset), read_f32(data, offset + 4), read_f32(data, offset + 8)};
}

std::optional<std::pair<std::string, std::size_t>> read_variable2(
    std::span<const std::byte> data, std::size_t offset) {
    if (offset + 2 > data.size()) return std::nullopt;
    const auto size = std::to_integer<std::size_t>(data[offset]) |
                      (std::to_integer<std::size_t>(data[offset + 1]) << 8);
    if (size == 0 || offset + 2 + size > data.size() || data[offset + 1 + size] != std::byte{})
        return std::nullopt;
    const auto begin = reinterpret_cast<const char*>(data.data() + offset + 2);
    return std::pair{std::string(begin, size - 1), offset + 2 + size};
}

void append_le_u32(std::vector<std::byte>& output, std::uint32_t value) {
    output.push_back(static_cast<std::byte>(value & 0xff));
    output.push_back(static_cast<std::byte>((value >> 8) & 0xff));
    output.push_back(static_cast<std::byte>((value >> 16) & 0xff));
    output.push_back(static_cast<std::byte>((value >> 24) & 0xff));
}

void append_le_u16(std::vector<std::byte>& output, std::uint16_t value) {
    output.push_back(static_cast<std::byte>(value));
    output.push_back(static_cast<std::byte>(value >> 8));
}

void append_le_u64(std::vector<std::byte>& output, std::uint64_t value) {
    append_le_u32(output, static_cast<std::uint32_t>(value));
    append_le_u32(output, static_cast<std::uint32_t>(value >> 32));
}

void append_f32(std::vector<std::byte>& output, float value) {
    static_assert(sizeof(float) == sizeof(std::uint32_t));
    std::uint32_t bits{};
    std::memcpy(&bits, &value, sizeof(bits));
    append_le_u32(output, bits);
}

void append_uuid(std::vector<std::byte>& output, const Uuid& value) {
    output.insert(output.end(), value.begin(), value.end());
}

bool append_variable1(std::vector<std::byte>& output, std::string_view value) {
    if (value.size() > 254) return false;
    output.push_back(static_cast<std::byte>(value.size() + 1));
    output.insert(output.end(), reinterpret_cast<const std::byte*>(value.data()),
                  reinterpret_cast<const std::byte*>(value.data() + value.size()));
    output.push_back(std::byte{});
    return true;
}

bool append_variable2(std::vector<std::byte>& output, std::string_view value) {
    if (value.size() > 65534) return false;
    const auto size = static_cast<std::uint16_t>(value.size() + 1);
    output.push_back(static_cast<std::byte>(size & 0xff));
    output.push_back(static_cast<std::byte>((size >> 8) & 0xff));
    output.insert(output.end(), reinterpret_cast<const std::byte*>(value.data()),
                  reinterpret_cast<const std::byte*>(value.data() + value.size()));
    output.push_back(std::byte{});
    return true;
}

bool append_binary(std::vector<std::byte>& output, std::span<const std::byte> value, unsigned length_bytes) {
    if ((length_bytes == 1 && value.size() > 255) || value.size() > 65535) return false;
    output.push_back(static_cast<std::byte>(value.size()));
    if (length_bytes == 2) output.push_back(static_cast<std::byte>(value.size() >> 8));
    output.insert(output.end(), value.begin(), value.end());
    return true;
}

std::vector<std::byte> zero_encode(std::span<const std::byte> input) {
    std::vector<std::byte> output;
    output.reserve(input.size());
    for (std::size_t index = 0; index < input.size();) {
        if (input[index] != std::byte{}) {
            output.push_back(input[index++]);
            continue;
        }
        std::size_t count = 0;
        while (index < input.size() && input[index] == std::byte{} && count < 255) {
            ++index;
            ++count;
        }
        output.push_back(std::byte{});
        output.push_back(static_cast<std::byte>(count));
    }
    return output;
}

std::optional<std::vector<std::byte>> zero_decode(std::span<const std::byte> input) {
    std::vector<std::byte> output;
    for (std::size_t index = 0; index < input.size(); ++index) {
        if (input[index] != std::byte{}) {
            output.push_back(input[index]);
            continue;
        }
        if (++index >= input.size()) return std::nullopt;
        const auto count = std::to_integer<unsigned>(input[index]);
        if (count == 0 || output.size() > 65535 - count) return std::nullopt;
        output.insert(output.end(), count, std::byte{});
    }
    return output;
}

} // namespace

std::optional<Uuid> parse_uuid(std::string_view text) {
    if (text.size() != 36 || text[8] != '-' || text[13] != '-' || text[18] != '-' || text[23] != '-')
        return std::nullopt;
    Uuid result;
    std::size_t input = 0;
    for (auto& byte : result) {
        if (input == 8 || input == 13 || input == 18 || input == 23) ++input;
        unsigned value{};
        const auto parsed = std::from_chars(text.data() + input, text.data() + input + 2, value, 16);
        if (parsed.ec != std::errc{} || parsed.ptr != text.data() + input + 2) return std::nullopt;
        byte = static_cast<std::byte>(value);
        input += 2;
    }
    return result;
}

std::string format_uuid(const Uuid& value) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(36);
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (index == 4 || index == 6 || index == 8 || index == 10) result.push_back('-');
        const auto byte = std::to_integer<unsigned>(value[index]);
        result.push_back(digits[byte >> 4]);
        result.push_back(digits[byte & 0x0f]);
    }
    return result;
}

Uuid combine_uuids(const Uuid& first, const Uuid& second) {
    constexpr std::array<std::uint32_t, 64> constants{
        0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
        0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
        0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
        0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
        0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
        0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
        0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
        0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};
    constexpr std::array<unsigned, 64> shifts{
        7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
        5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
        4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
        6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};
    std::array<std::byte, 64> block{};
    std::copy(first.begin(), first.end(), block.begin());
    std::copy(second.begin(), second.end(), block.begin() + 16);
    block[32] = std::byte{0x80};
    block[57] = std::byte{0x01}; // 32 bytes = 256 bits, little endian
    std::array<std::uint32_t, 16> words{};
    for (std::size_t index = 0; index < words.size(); ++index) {
        const auto offset = index * 4;
        words[index] = std::to_integer<std::uint32_t>(block[offset]) |
                       (std::to_integer<std::uint32_t>(block[offset + 1]) << 8) |
                       (std::to_integer<std::uint32_t>(block[offset + 2]) << 16) |
                       (std::to_integer<std::uint32_t>(block[offset + 3]) << 24);
    }
    std::uint32_t a = 0x67452301;
    std::uint32_t b = 0xefcdab89;
    std::uint32_t c = 0x98badcfe;
    std::uint32_t d = 0x10325476;
    const auto initial_a = a;
    const auto initial_b = b;
    const auto initial_c = c;
    const auto initial_d = d;
    for (std::uint32_t index = 0; index < 64; ++index) {
        std::uint32_t function{};
        std::uint32_t word{};
        if (index < 16) {
            function = (b & c) | (~b & d);
            word = index;
        } else if (index < 32) {
            function = (d & b) | (~d & c);
            word = (5 * index + 1) % 16;
        } else if (index < 48) {
            function = b ^ c ^ d;
            word = (3 * index + 5) % 16;
        } else {
            function = c ^ (b | ~d);
            word = (7 * index) % 16;
        }
        const auto next_d = d;
        d = c;
        c = b;
        b += std::rotl(a + function + constants[index] + words[word], shifts[index]);
        a = next_d;
    }
    const std::array<std::uint32_t, 4> digest{
        initial_a + a, initial_b + b, initial_c + c, initial_d + d};
    Uuid result{};
    for (std::size_t index = 0; index < digest.size(); ++index)
        for (std::size_t byte = 0; byte < 4; ++byte)
            result[index * 4 + byte] = static_cast<std::byte>(digest[index] >> (byte * 8));
    return result;
}

std::vector<std::byte> encode_use_circuit_code(const UseCircuitCode& message) {
    std::vector<std::byte> output(use_circuit_code_id.begin(), use_circuit_code_id.end());
    output.reserve(40);
    append_le_u32(output, message.circuit_code);
    output.insert(output.end(), message.session_id.begin(), message.session_id.end());
    output.insert(output.end(), message.agent_id.begin(), message.agent_id.end());
    return output;
}

std::optional<UseCircuitCode> decode_use_circuit_code(std::span<const std::byte> payload) {
    if (payload.size() != 40 || !std::equal(use_circuit_code_id.begin(), use_circuit_code_id.end(), payload.begin()))
        return std::nullopt;
    UseCircuitCode message;
    message.circuit_code = read_le_u32(payload, 4);
    std::copy_n(payload.begin() + 8, 16, message.session_id.begin());
    std::copy_n(payload.begin() + 24, 16, message.agent_id.begin());
    return message;
}

std::vector<std::byte> encode_region_handshake(const RegionHandshake& message) {
    std::vector<std::byte> output(region_handshake_id.begin(), region_handshake_id.end());
    append_le_u32(output, 0); // region flags
    output.push_back(std::byte{13}); // PG access
    if (!append_variable1(output, message.name)) return {};
    append_uuid(output, message.owner_id);
    output.push_back(std::byte{}); // estate manager
    append_f32(output, message.water_height);
    append_f32(output, 1.0F); // billable factor
    Uuid zero{};
    append_uuid(output, zero); // cache ID
    for (const auto& texture : message.terrain_textures) append_uuid(output, texture); // terrain base
    for (const auto& texture : message.terrain_textures) append_uuid(output, texture); // terrain detail
    for (const float value : {10.F, 10.F, 10.F, 10.F, 60.F, 60.F, 60.F, 60.F}) append_f32(output, value);
    append_uuid(output, message.region_id);
    append_le_u32(output, 0); // CPU class
    append_le_u32(output, 1); // CPU ratio
    if (!append_variable1(output, "") || !append_variable1(output, "homeworldz") ||
        !append_variable1(output, "HomeWorldz Region")) return {};
    output.push_back(std::byte{}); // no RegionInfo4 blocks
    return output;
}

std::optional<AgentMessage> decode_region_handshake_reply(std::span<const std::byte> payload) {
    if (payload.size() != 40 || !std::equal(region_handshake_reply_id.begin(), region_handshake_reply_id.end(), payload.begin()))
        return std::nullopt;
    AgentMessage result;
    std::copy_n(payload.begin() + 4, 16, result.agent_id.begin());
    std::copy_n(payload.begin() + 20, 16, result.session_id.begin());
    return result;
}

std::optional<CompleteAgentMovement> decode_complete_agent_movement(std::span<const std::byte> payload) {
    if (payload.size() != 40 || !std::equal(complete_agent_movement_id.begin(), complete_agent_movement_id.end(), payload.begin()))
        return std::nullopt;
    CompleteAgentMovement result;
    std::copy_n(payload.begin() + 4, 16, result.agent_id.begin());
    std::copy_n(payload.begin() + 20, 16, result.session_id.begin());
    result.circuit_code = read_le_u32(payload, 36);
    return result;
}

std::vector<std::byte> encode_agent_movement_complete(const AgentMovementComplete& message) {
    std::vector<std::byte> output(agent_movement_complete_id.begin(), agent_movement_complete_id.end());
    append_uuid(output, message.agent_id);
    append_uuid(output, message.session_id);
    for (const auto value : message.position) append_f32(output, value);
    for (const auto value : message.look_at) append_f32(output, value);
    append_le_u64(output, message.region_handle);
    append_le_u32(output, message.timestamp);
    if (!append_variable2(output, message.channel_version)) return {};
    return output;
}

std::vector<std::byte> encode_start_ping_check(std::uint8_t ping_id, std::uint32_t oldest_unacked) {
    std::vector<std::byte> output{std::byte{1}, static_cast<std::byte>(ping_id)};
    append_le_u32(output, oldest_unacked);
    return output;
}

std::optional<std::uint8_t> decode_start_ping_check(std::span<const std::byte> payload) {
    if (payload.size() != 6 || payload[0] != std::byte{1}) return std::nullopt;
    return std::to_integer<std::uint8_t>(payload[1]);
}

std::vector<std::byte> encode_complete_ping_check(std::uint8_t ping_id) {
    return {std::byte{2}, static_cast<std::byte>(ping_id)};
}

bool is_economy_data_request(std::span<const std::byte> payload) {
    return payload.size() == economy_data_request_id.size() &&
           std::equal(economy_data_request_id.begin(), economy_data_request_id.end(), payload.begin());
}

std::vector<std::byte> encode_economy_data(std::int32_t price_upload,
                                           std::int32_t object_capacity,
                                           std::int32_t object_count) {
    std::vector<std::byte> output(economy_data_id.begin(), economy_data_id.end());
    const auto integer = [&output](std::int32_t value) {
        append_le_u32(output, static_cast<std::uint32_t>(value));
    };
    integer(object_capacity);
    integer(object_count);
    integer(0); // price energy unit
    integer(0); // price object claim
    integer(0); // price public object decay
    integer(0); // price public object delete
    integer(0); // price parcel claim
    append_f32(output, 0.0F); // price parcel claim factor
    integer(price_upload);
    integer(0); // price rent light
    integer(0); // teleport minimum price
    append_f32(output, 0.0F); // teleport price exponent
    append_f32(output, 1.0F); // energy efficiency
    append_f32(output, 0.0F); // price object rent
    append_f32(output, 0.0F); // price object scale factor
    integer(0); // price parcel rent
    integer(0); // price group create
    return output;
}

std::optional<AgentMessage> decode_logout_request(std::span<const std::byte> payload) {
    if (payload.size() != 36 || !std::equal(logout_request_id.begin(), logout_request_id.end(), payload.begin()))
        return std::nullopt;
    AgentMessage result;
    std::copy_n(payload.begin() + 4, 16, result.agent_id.begin());
    std::copy_n(payload.begin() + 20, 16, result.session_id.begin());
    return result;
}

std::optional<CreateInventoryFolder> decode_create_inventory_folder(std::span<const std::byte> payload) {
    constexpr std::size_t fixed_size = 70;
    if (payload.size() < fixed_size ||
        !std::equal(create_inventory_folder_id.begin(), create_inventory_folder_id.end(), payload.begin()))
        return std::nullopt;
    const auto name_size = std::to_integer<std::size_t>(payload[69]);
    if (name_size == 0 || payload.size() != fixed_size + name_size) return std::nullopt;
    CreateInventoryFolder result;
    std::copy_n(payload.begin() + 4, 16, result.agent_id.begin());
    std::copy_n(payload.begin() + 20, 16, result.session_id.begin());
    std::copy_n(payload.begin() + 36, 16, result.folder_id.begin());
    std::copy_n(payload.begin() + 52, 16, result.parent_id.begin());
    result.type = static_cast<std::int8_t>(std::to_integer<std::uint8_t>(payload[68]));
    result.name.assign(reinterpret_cast<const char*>(payload.data() + fixed_size), name_size);
    while (!result.name.empty() && result.name.back() == '\0') result.name.pop_back();
    if (result.name.empty() || result.name.find('\0') != std::string::npos) return std::nullopt;
    return result;
}

std::optional<CopyInventoryItem> decode_copy_inventory_item(std::span<const std::byte> payload) {
    constexpr std::size_t fixed_size = 90;
    if (payload.size() < fixed_size ||
        !std::equal(copy_inventory_item_id.begin(), copy_inventory_item_id.end(), payload.begin()) ||
        payload[36] != std::byte{1})
        return std::nullopt;
    const auto name_size = std::to_integer<std::size_t>(payload[89]);
    if (name_size == 0 || payload.size() != fixed_size + name_size) return std::nullopt;
    CopyInventoryItem result;
    std::copy_n(payload.begin() + 4, 16, result.agent_id.begin());
    std::copy_n(payload.begin() + 20, 16, result.session_id.begin());
    result.callback_id = read_le_u32(payload, 37);
    std::copy_n(payload.begin() + 41, 16, result.old_agent_id.begin());
    std::copy_n(payload.begin() + 57, 16, result.old_item_id.begin());
    std::copy_n(payload.begin() + 73, 16, result.new_folder_id.begin());
    result.new_name.assign(reinterpret_cast<const char*>(payload.data() + fixed_size), name_size);
    while (!result.new_name.empty() && result.new_name.back() == '\0') result.new_name.pop_back();
    if (result.new_name.find('\0') != std::string::npos) return std::nullopt;
    return result;
}

std::optional<MoveInventoryFolder> decode_move_inventory_folder(std::span<const std::byte> payload) {
    constexpr std::size_t header_size = 38;
    constexpr std::size_t block_size = 32;
    if (payload.size() < header_size ||
        !std::equal(move_inventory_folder_id.begin(), move_inventory_folder_id.end(), payload.begin()))
        return std::nullopt;
    const auto count = std::to_integer<std::size_t>(payload[37]);
    if (count == 0 || payload.size() != header_size + count * block_size) return std::nullopt;
    MoveInventoryFolder result;
    std::copy_n(payload.begin() + 4, 16, result.agent_id.begin());
    std::copy_n(payload.begin() + 20, 16, result.session_id.begin());
    result.stamp = payload[36] != std::byte{};
    result.folders.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        const auto offset = header_size + index * block_size;
        InventoryFolderMove move;
        std::copy_n(payload.begin() + offset, 16, move.folder_id.begin());
        std::copy_n(payload.begin() + offset + 16, 16, move.parent_id.begin());
        result.folders.push_back(move);
    }
    return result;
}

std::optional<MoveInventoryItem> decode_move_inventory_item(std::span<const std::byte> payload) {
    constexpr std::size_t header_size = 38;
    constexpr std::size_t fixed_block_size = 33;
    if (payload.size() < header_size ||
        !std::equal(move_inventory_item_id.begin(), move_inventory_item_id.end(), payload.begin()))
        return std::nullopt;
    const auto count = std::to_integer<std::size_t>(payload[37]);
    if (count == 0) return std::nullopt;
    MoveInventoryItem result;
    std::copy_n(payload.begin() + 4, 16, result.agent_id.begin());
    std::copy_n(payload.begin() + 20, 16, result.session_id.begin());
    result.stamp = payload[36] != std::byte{};
    result.items.reserve(count);
    std::size_t offset = header_size;
    for (std::size_t index = 0; index < count; ++index) {
        if (payload.size() < offset + fixed_block_size) return std::nullopt;
        InventoryItemMove move;
        std::copy_n(payload.begin() + offset, 16, move.item_id.begin());
        std::copy_n(payload.begin() + offset + 16, 16, move.folder_id.begin());
        const auto name_size = std::to_integer<std::size_t>(payload[offset + 32]);
        if (payload.size() < offset + fixed_block_size + name_size)
            return std::nullopt;
        move.new_name.assign(
            reinterpret_cast<const char*>(payload.data() + offset + fixed_block_size), name_size);
        while (!move.new_name.empty() && move.new_name.back() == '\0') move.new_name.pop_back();
        if (move.new_name.find('\0') != std::string::npos) return std::nullopt;
        result.items.push_back(std::move(move));
        offset += fixed_block_size + name_size;
    }
    if (offset != payload.size()) return std::nullopt;
    return result;
}

std::optional<ObjectAdd> decode_object_add(std::span<const std::byte> payload) {
    constexpr std::size_t message_size = 146;
    if (payload.size() != message_size ||
        !std::equal(object_add_id.begin(), object_add_id.end(), payload.begin()))
        return std::nullopt;
    ObjectAdd result;
    std::copy_n(payload.begin() + 2, 16, result.agent_id.begin());
    std::copy_n(payload.begin() + 18, 16, result.session_id.begin());
    std::copy_n(payload.begin() + 34, 16, result.group_id.begin());
    result.pcode = std::to_integer<std::uint8_t>(payload[50]);
    result.material = std::to_integer<std::uint8_t>(payload[51]);
    result.add_flags = read_le_u32(payload, 52);
    result.path_curve = std::to_integer<std::uint8_t>(payload[56]);
    result.profile_curve = std::to_integer<std::uint8_t>(payload[57]);
    result.bypass_raycast = payload[79] != std::byte{};
    result.ray_start = read_vector3(payload, 80);
    result.ray_end = read_vector3(payload, 92);
    std::copy_n(payload.begin() + 104, 16, result.ray_target_id.begin());
    result.ray_end_is_intersection = payload[120] != std::byte{};
    result.scale = read_vector3(payload, 121);
    result.rotation = read_vector3(payload, 133);
    result.state = std::to_integer<std::uint8_t>(payload[145]);
    const auto finite_vector = [](const auto& values) {
        return std::all_of(values.begin(), values.end(), [](float value) { return std::isfinite(value); });
    };
    if (!finite_vector(result.ray_start) || !finite_vector(result.ray_end) ||
        !finite_vector(result.scale) || !finite_vector(result.rotation))
        return std::nullopt;
    return result;
}

std::optional<DeRezObject> decode_derez_object(std::span<const std::byte> payload) {
    constexpr std::size_t header_size = 88;
    if (payload.size() < header_size ||
        !std::equal(derez_object_id.begin(), derez_object_id.end(), payload.begin()))
        return std::nullopt;
    const auto count = std::to_integer<std::size_t>(payload[87]);
    if (count == 0 || payload.size() != header_size + count * sizeof(std::uint32_t))
        return std::nullopt;
    DeRezObject result;
    std::copy_n(payload.begin() + 4, 16, result.agent_id.begin());
    std::copy_n(payload.begin() + 20, 16, result.session_id.begin());
    std::copy_n(payload.begin() + 36, 16, result.group_id.begin());
    result.destination = std::to_integer<std::uint8_t>(payload[52]);
    std::copy_n(payload.begin() + 53, 16, result.destination_id.begin());
    std::copy_n(payload.begin() + 69, 16, result.transaction_id.begin());
    result.packet_count = std::to_integer<std::uint8_t>(payload[85]);
    result.packet_number = std::to_integer<std::uint8_t>(payload[86]);
    result.local_ids.reserve(count);
    for (std::size_t index = 0; index < count; ++index)
        result.local_ids.push_back(read_le_u32(payload, header_size + index * sizeof(std::uint32_t)));
    return result;
}

bool valid_derez_batch(std::uint8_t packet_count, std::uint8_t packet_number) {
    return packet_count > 0 && packet_number <= packet_count;
}

std::optional<RezObject> decode_rez_object(std::span<const std::byte> payload) {
    constexpr std::size_t minimum_size = 144;
    if (payload.size() < minimum_size ||
        !std::equal(rez_object_id.begin(), rez_object_id.end(), payload.begin()))
        return std::nullopt;
    RezObject result;
    std::copy_n(payload.begin() + 4, 16, result.agent_id.begin());
    std::copy_n(payload.begin() + 20, 16, result.session_id.begin());
    std::copy_n(payload.begin() + 36, 16, result.group_id.begin());
    std::copy_n(payload.begin() + 52, 16, result.from_task_id.begin());
    result.bypass_raycast = std::to_integer<std::uint8_t>(payload[68]);
    for (std::size_t axis = 0; axis < 3; ++axis) {
        result.ray_start[axis] = read_f32(payload, 69 + axis * sizeof(float));
        result.ray_end[axis] = read_f32(payload, 81 + axis * sizeof(float));
    }
    std::copy_n(payload.begin() + 93, 16, result.ray_target_id.begin());
    result.ray_end_is_intersection = payload[109] != std::byte{};
    result.rez_selected = payload[110] != std::byte{};
    result.remove_item = payload[111] != std::byte{};
    std::copy_n(payload.begin() + 128, 16, result.item_id.begin());
    const auto finite = [](const auto& values) {
        return std::all_of(values.begin(), values.end(), [](float value) { return std::isfinite(value); });
    };
    if (!finite(result.ray_start) || !finite(result.ray_end)) return std::nullopt;
    return result;
}

std::vector<std::byte> encode_kill_object(std::span<const std::uint32_t> local_ids) {
    if (local_ids.empty() || local_ids.size() > 255) return {};
    std::vector<std::byte> output{std::byte{0x10}, static_cast<std::byte>(local_ids.size())};
    output.reserve(2 + local_ids.size() * sizeof(std::uint32_t));
    for (const auto local_id : local_ids) append_le_u32(output, local_id);
    return output;
}

std::optional<ObjectSelect> decode_object_select(std::span<const std::byte> payload) {
    constexpr std::size_t header_size = 37;
    if (payload.size() < header_size ||
        !std::equal(object_select_id.begin(), object_select_id.end(), payload.begin()))
        return std::nullopt;
    const auto count = std::to_integer<std::size_t>(payload[36]);
    if (count == 0 || payload.size() != header_size + count * sizeof(std::uint32_t))
        return std::nullopt;
    ObjectSelect result;
    std::copy_n(payload.begin() + 4, 16, result.agent_id.begin());
    std::copy_n(payload.begin() + 20, 16, result.session_id.begin());
    result.local_ids.reserve(count);
    for (std::size_t index = 0; index < count; ++index)
        result.local_ids.push_back(read_le_u32(payload, header_size + index * sizeof(std::uint32_t)));
    return result;
}

std::optional<MultipleObjectUpdate> decode_multiple_object_update(std::span<const std::byte> payload) {
    constexpr std::uint8_t update_position = 0x01;
    constexpr std::uint8_t update_rotation = 0x02;
    constexpr std::uint8_t update_scale = 0x04;
    constexpr std::uint8_t known_flags = 0x1f;
    constexpr std::size_t header_size = 35;
    if (payload.size() < header_size ||
        !std::equal(multiple_object_update_id.begin(), multiple_object_update_id.end(), payload.begin()))
        return std::nullopt;
    MultipleObjectUpdate result;
    std::copy_n(payload.begin() + 2, 16, result.agent_id.begin());
    std::copy_n(payload.begin() + 18, 16, result.session_id.begin());
    const auto count = std::to_integer<std::size_t>(payload[34]);
    if (count == 0) return std::nullopt;
    result.objects.reserve(count);
    std::size_t offset = header_size;
    for (std::size_t index = 0; index < count; ++index) {
        if (offset + 6 > payload.size()) return std::nullopt;
        ObjectTransformUpdate update;
        update.local_id = read_le_u32(payload, offset);
        update.type = std::to_integer<std::uint8_t>(payload[offset + 4]);
        const auto data_size = std::to_integer<std::size_t>(payload[offset + 5]);
        offset += 6;
        const auto vector_count = static_cast<std::size_t>((update.type & update_position) != 0) +
            static_cast<std::size_t>((update.type & update_rotation) != 0) +
            static_cast<std::size_t>((update.type & update_scale) != 0);
        if ((update.type & ~known_flags) != 0 || vector_count == 0 ||
            data_size != vector_count * 12 || offset + data_size > payload.size())
            return std::nullopt;
        const auto read_vector = [&]() {
            std::array<float, 3> value{
                read_f32(payload, offset), read_f32(payload, offset + 4), read_f32(payload, offset + 8)};
            offset += 12;
            return value;
        };
        if ((update.type & update_position) != 0) update.position = read_vector();
        if ((update.type & update_rotation) != 0) update.rotation = read_vector();
        if ((update.type & update_scale) != 0) update.scale = read_vector();
        result.objects.push_back(update);
    }
    if (offset != payload.size()) return std::nullopt;
    return result;
}

std::optional<ObjectName> decode_object_name(std::span<const std::byte> payload) {
    constexpr std::size_t header_size = 37;
    if (payload.size() < header_size ||
        !std::equal(object_name_id.begin(), object_name_id.end(), payload.begin()))
        return std::nullopt;
    ObjectName result;
    std::copy_n(payload.begin() + 4, 16, result.agent_id.begin());
    std::copy_n(payload.begin() + 20, 16, result.session_id.begin());
    const auto count = std::to_integer<std::size_t>(payload[36]);
    if (count == 0) return std::nullopt;
    result.objects.reserve(count);
    std::size_t offset = header_size;
    for (std::size_t index = 0; index < count; ++index) {
        if (offset + 5 > payload.size()) return std::nullopt;
        ObjectNameUpdate update;
        update.local_id = read_le_u32(payload, offset);
        const auto encoded_size = std::to_integer<std::size_t>(payload[offset + 4]);
        offset += 5;
        if (encoded_size < 2 || encoded_size > 64 || offset + encoded_size > payload.size() ||
            payload[offset + encoded_size - 1] != std::byte{})
            return std::nullopt;
        update.name.assign(reinterpret_cast<const char*>(payload.data() + offset), encoded_size - 1);
        offset += encoded_size;
        result.objects.push_back(std::move(update));
    }
    if (offset != payload.size()) return std::nullopt;
    return result;
}

std::optional<ObjectDescription> decode_object_description(std::span<const std::byte> payload) {
    constexpr std::size_t header_size = 37;
    if (payload.size() < header_size ||
        !std::equal(object_description_id.begin(), object_description_id.end(), payload.begin()))
        return std::nullopt;
    ObjectDescription result;
    std::copy_n(payload.begin() + 4, 16, result.agent_id.begin());
    std::copy_n(payload.begin() + 20, 16, result.session_id.begin());
    const auto count = std::to_integer<std::size_t>(payload[36]);
    if (count == 0) return std::nullopt;
    result.objects.reserve(count);
    std::size_t offset = header_size;
    for (std::size_t index = 0; index < count; ++index) {
        if (offset + 5 > payload.size()) return std::nullopt;
        ObjectDescriptionUpdate update;
        update.local_id = read_le_u32(payload, offset);
        const auto encoded_size = std::to_integer<std::size_t>(payload[offset + 4]);
        offset += 5;
        if (encoded_size == 0 || encoded_size > 128 || offset + encoded_size > payload.size() ||
            payload[offset + encoded_size - 1] != std::byte{})
            return std::nullopt;
        update.description.assign(
            reinterpret_cast<const char*>(payload.data() + offset), encoded_size - 1);
        offset += encoded_size;
        result.objects.push_back(std::move(update));
    }
    if (offset != payload.size()) return std::nullopt;
    return result;
}

std::optional<ObjectPermissions> decode_object_permissions(std::span<const std::byte> payload) {
    constexpr std::size_t header_size = 38;
    constexpr std::size_t block_size = 10;
    if (payload.size() < header_size ||
        !std::equal(object_permissions_id.begin(), object_permissions_id.end(), payload.begin()))
        return std::nullopt;
    ObjectPermissions result;
    std::copy_n(payload.begin() + 4, 16, result.agent_id.begin());
    std::copy_n(payload.begin() + 20, 16, result.session_id.begin());
    const auto override_value = std::to_integer<std::uint8_t>(payload[36]);
    if (override_value > 1) return std::nullopt;
    result.override_permissions = override_value != 0;
    const auto count = std::to_integer<std::size_t>(payload[37]);
    if (count == 0 || payload.size() != header_size + count * block_size) return std::nullopt;
    result.objects.reserve(count);
    std::size_t offset = header_size;
    for (std::size_t index = 0; index < count; ++index) {
        ObjectPermissionUpdate update;
        update.local_id = read_le_u32(payload, offset);
        update.field = std::to_integer<std::uint8_t>(payload[offset + 4]);
        const auto set_value = std::to_integer<std::uint8_t>(payload[offset + 5]);
        if (set_value > 1) return std::nullopt;
        update.set = set_value != 0;
        update.mask = read_le_u32(payload, offset + 6);
        result.objects.push_back(update);
        offset += block_size;
    }
    return result;
}

std::optional<ObjectDuplicate> decode_object_duplicate(std::span<const std::byte> payload) {
    constexpr std::size_t header_size = 69;
    if (payload.size() < header_size ||
        !std::equal(object_duplicate_id.begin(), object_duplicate_id.end(), payload.begin()))
        return std::nullopt;
    ObjectDuplicate result;
    std::copy_n(payload.begin() + 4, 16, result.agent_id.begin());
    std::copy_n(payload.begin() + 20, 16, result.session_id.begin());
    std::copy_n(payload.begin() + 36, 16, result.group_id.begin());
    result.offset = {read_f32(payload, 52), read_f32(payload, 56), read_f32(payload, 60)};
    result.duplicate_flags = read_le_u32(payload, 64);
    const auto count = std::to_integer<std::size_t>(payload[68]);
    if (count == 0 || payload.size() != header_size + count * sizeof(std::uint32_t))
        return std::nullopt;
    result.local_ids.reserve(count);
    for (std::size_t index = 0; index < count; ++index)
        result.local_ids.push_back(read_le_u32(payload, header_size + index * sizeof(std::uint32_t)));
    return result;
}

std::optional<ObjectMaterial> decode_object_material(std::span<const std::byte> payload) {
    constexpr std::size_t header_size = 37;
    constexpr std::size_t block_size = 5;
    if (payload.size() < header_size ||
        !std::equal(object_material_id.begin(), object_material_id.end(), payload.begin()))
        return std::nullopt;
    ObjectMaterial result;
    std::copy_n(payload.begin() + 4, 16, result.agent_id.begin());
    std::copy_n(payload.begin() + 20, 16, result.session_id.begin());
    const auto count = std::to_integer<std::size_t>(payload[36]);
    if (count == 0 || payload.size() != header_size + count * block_size) return std::nullopt;
    result.objects.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        const auto offset = header_size + index * block_size;
        result.objects.push_back({
            read_le_u32(payload, offset), std::to_integer<std::uint8_t>(payload[offset + 4])});
    }
    return result;
}

std::optional<RequestObjectPropertiesFamily> decode_request_object_properties_family(
    std::span<const std::byte> payload) {
    if (payload.size() != 54 ||
        !std::equal(request_object_properties_family_id.begin(),
                    request_object_properties_family_id.end(), payload.begin()))
        return std::nullopt;
    RequestObjectPropertiesFamily result;
    std::copy_n(payload.begin() + 2, 16, result.agent_id.begin());
    std::copy_n(payload.begin() + 18, 16, result.session_id.begin());
    result.request_flags = read_le_u32(payload, 34);
    std::copy_n(payload.begin() + 38, 16, result.object_id.begin());
    return result;
}

std::vector<std::byte> encode_object_properties(std::span<const ObjectProperties> objects) {
    if (objects.empty() || objects.size() > 255) return {};
    std::vector<std::byte> output{std::byte{0xff}, std::byte{0x09},
                                  static_cast<std::byte>(objects.size())};
    Uuid zero{};
    for (const auto& object : objects) {
        append_uuid(output, object.object_id);
        append_uuid(output, object.creator_id);
        append_uuid(output, object.owner_id);
        append_uuid(output, zero); // group
        append_le_u64(output, object.creation_date * 1000000); // protocol uses microseconds
        append_le_u32(output, object.base_permissions);
        append_le_u32(output, object.owner_permissions);
        append_le_u32(output, object.group_permissions);
        append_le_u32(output, object.everyone_permissions);
        append_le_u32(output, object.next_owner_permissions);
        append_le_u32(output, 0); // ownership cost
        output.push_back(std::byte{}); // not for sale
        append_le_u32(output, 0); // sale price
        std::uint8_t aggregate_permissions = 0;
        if ((object.owner_permissions & 0x00008000) != 0) aggregate_permissions |= 0x03; // copy
        if ((object.owner_permissions & 0x00004000) != 0) aggregate_permissions |= 0x0c; // modify
        if ((object.owner_permissions & 0x00002000) != 0) aggregate_permissions |= 0x30; // transfer
        output.push_back(static_cast<std::byte>(aggregate_permissions));
        output.insert(output.end(), 2, std::byte{}); // aggregate texture permissions
        append_le_u32(output, 0); // category
        append_le_u16(output, 0); // inventory serial
        for (int index = 0; index < 3; ++index) append_uuid(output, zero); // inventory IDs
        append_uuid(output, object.creator_id); // initial owner is also the last owner
        if (!append_variable1(output, object.name) || !append_variable1(output, object.description) ||
            !append_variable1(output, {}) || !append_variable1(output, {}) || !append_variable1(output, {}))
            return {};
    }
    return output;
}

std::vector<std::byte> encode_object_properties_family(
    std::uint32_t request_flags, const ObjectProperties& object) {
    std::vector<std::byte> output{std::byte{0xff}, std::byte{0x0a}};
    Uuid zero{};
    append_le_u32(output, request_flags);
    append_uuid(output, object.object_id);
    append_uuid(output, object.owner_id);
    append_uuid(output, zero); // group
    append_le_u32(output, object.base_permissions);
    append_le_u32(output, object.owner_permissions);
    append_le_u32(output, object.group_permissions);
    append_le_u32(output, object.everyone_permissions);
    append_le_u32(output, object.next_owner_permissions);
    append_le_u32(output, 0); // ownership cost
    output.push_back(std::byte{}); // not for sale
    append_le_u32(output, 0); // sale price
    append_le_u32(output, 0); // category
    append_uuid(output, object.creator_id); // initial owner is also the last owner
    if (!append_variable1(output, object.name) || !append_variable1(output, object.description)) return {};
    return output;
}

std::optional<std::vector<Uuid>> decode_uuid_name_request(std::span<const std::byte> payload) {
    constexpr std::size_t header_size = 5;
    if (payload.size() < header_size ||
        !std::equal(uuid_name_request_id.begin(), uuid_name_request_id.end(), payload.begin()))
        return std::nullopt;
    const auto count = std::to_integer<std::size_t>(payload[4]);
    if (count == 0 || payload.size() != header_size + count * 16) return std::nullopt;
    std::vector<Uuid> result(count);
    for (std::size_t index = 0; index < count; ++index)
        std::copy_n(payload.begin() + header_size + index * 16, 16, result[index].begin());
    return result;
}

std::vector<std::byte> encode_uuid_name_reply(std::span<const UuidName> names) {
    if (names.empty() || names.size() > 255) return {};
    std::vector<std::byte> output(uuid_name_reply_id.begin(), uuid_name_reply_id.end());
    output.push_back(static_cast<std::byte>(names.size()));
    for (const auto& name : names) {
        append_uuid(output, name.id);
        if (!append_variable1(output, name.first_name) || !append_variable1(output, name.last_name))
            return {};
    }
    return output;
}

std::vector<std::byte> encode_update_create_inventory_item(const AgentMessage& message,
                                                           std::uint32_t callback_id,
                                                           const InventoryItem& item) {
    std::vector<std::byte> output(update_create_inventory_item_id.begin(),
                                  update_create_inventory_item_id.end());
    append_uuid(output, message.agent_id);
    output.push_back(std::byte{1}); // simulator approved
    append_uuid(output, Uuid{}); // no transaction ID for an inventory copy
    output.push_back(std::byte{1}); // InventoryData block count
    append_uuid(output, item.item_id);
    append_uuid(output, item.folder_id);
    append_le_u32(output, callback_id);
    append_uuid(output, item.creator_id);
    append_uuid(output, item.owner_id);
    append_uuid(output, Uuid{}); // group ID
    append_le_u32(output, item.base_permissions);
    append_le_u32(output, item.current_permissions);
    append_le_u32(output, 0); // group permissions
    append_le_u32(output, item.everyone_permissions);
    append_le_u32(output, item.next_permissions);
    output.push_back(std::byte{0}); // not group owned
    append_uuid(output, item.asset_id);
    output.push_back(static_cast<std::byte>(item.asset_type));
    output.push_back(static_cast<std::byte>(item.inventory_type));
    append_le_u32(output, item.flags);
    output.push_back(static_cast<std::byte>(item.sale_type));
    append_le_u32(output, static_cast<std::uint32_t>(item.sale_price));
    if (!append_variable1(output, item.name) || !append_variable1(output, item.description)) return {};
    append_le_u32(output, static_cast<std::uint32_t>(item.creation_date));
    append_le_u32(output, 0); // viewer does not validate inventory CRC
    return output;
}

std::vector<std::byte> encode_logout_reply(const AgentMessage& message) {
    std::vector<std::byte> output(logout_reply_id.begin(), logout_reply_id.end());
    append_uuid(output, message.agent_id);
    append_uuid(output, message.session_id);
    output.push_back(std::byte{1}); // InventoryData block count
    append_uuid(output, Uuid{}); // no changed inventory items
    return output;
}

std::optional<AgentCachedTexture> decode_agent_cached_texture(std::span<const std::byte> payload) {
    constexpr std::size_t header_size = 41;
    constexpr std::size_t block_size = 17;
    if (payload.size() < header_size ||
        !std::equal(agent_cached_texture_id.begin(), agent_cached_texture_id.end(), payload.begin()))
        return std::nullopt;
    const auto count = std::to_integer<std::size_t>(payload[40]);
    if (count == 0 || payload.size() != header_size + count * block_size) return std::nullopt;
    AgentCachedTexture result;
    std::copy_n(payload.begin() + 4, 16, result.agent_id.begin());
    std::copy_n(payload.begin() + 20, 16, result.session_id.begin());
    result.serial = static_cast<std::int32_t>(read_le_u32(payload, 36));
    result.queries.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        const auto offset = header_size + index * block_size;
        CachedTextureQuery query;
        std::copy_n(payload.begin() + offset, 16, query.cache_id.begin());
        query.texture_index = std::to_integer<std::uint8_t>(payload[offset + 16]);
        result.queries.push_back(query);
    }
    return result;
}

std::vector<std::byte> encode_agent_cached_texture_response(const AgentCachedTexture& message) {
    if (message.queries.empty() || message.queries.size() > 255) return {};
    std::vector<std::byte> output(agent_cached_texture_response_id.begin(), agent_cached_texture_response_id.end());
    append_uuid(output, message.agent_id);
    append_uuid(output, message.session_id);
    append_le_u32(output, static_cast<std::uint32_t>(message.serial));
    output.push_back(static_cast<std::byte>(message.queries.size()));
    for (const auto& query : message.queries) {
        append_uuid(output, query.texture_id);
        output.push_back(static_cast<std::byte>(query.texture_index));
        if (!append_variable1(output, "")) return {};
    }
    return output;
}

std::optional<AgentSetAppearance> decode_agent_set_appearance(std::span<const std::byte> payload) {
    constexpr std::size_t fixed_size = 53;
    constexpr std::size_t cache_block_size = 17;
    if (payload.size() < fixed_size ||
        !std::equal(agent_set_appearance_id.begin(), agent_set_appearance_id.end(), payload.begin()))
        return std::nullopt;
    AgentSetAppearance result;
    std::copy_n(payload.begin() + 4, 16, result.agent_id.begin());
    std::copy_n(payload.begin() + 20, 16, result.session_id.begin());
    result.serial = read_le_u32(payload, 36);
    for (std::size_t axis = 0; axis < result.size.size(); ++axis)
        result.size[axis] = read_f32(payload, 40 + axis * 4);
    const auto cache_count = std::to_integer<std::size_t>(payload[52]);
    auto position = fixed_size;
    if (position + cache_count * cache_block_size + 2 > payload.size()) return std::nullopt;
    result.cache_entries.reserve(cache_count);
    for (std::size_t index = 0; index < cache_count; ++index) {
        CachedTextureQuery entry;
        std::copy_n(payload.begin() + position, 16, entry.cache_id.begin());
        entry.texture_index = std::to_integer<std::uint8_t>(payload[position + 16]);
        result.cache_entries.push_back(entry);
        position += cache_block_size;
    }
    const auto texture_entry_size = std::to_integer<std::size_t>(payload[position]) |
                                    (std::to_integer<std::size_t>(payload[position + 1]) << 8);
    position += 2;
    if (position + texture_entry_size + 1 > payload.size()) return std::nullopt;
    const auto texture_entry = payload.subspan(position, texture_entry_size);
    position += texture_entry_size;
    const auto visual_count = std::to_integer<std::size_t>(payload[position++]);
    if (position + visual_count != payload.size()) return std::nullopt;
    result.visual_params.reserve(visual_count);
    for (std::size_t index = 0; index < visual_count; ++index)
        result.visual_params.push_back(std::to_integer<std::uint8_t>(payload[position + index]));
    if (texture_entry.size() >= 16) {
        Uuid default_id;
        std::copy_n(texture_entry.begin(), 16, default_id.begin());
        result.texture_ids.fill(default_id);
        std::size_t texture_position = 16;
        while (texture_position < texture_entry.size()) {
            std::uint32_t faces = 0;
            unsigned face_bits = 0;
            std::uint8_t value{};
            do {
                if (texture_position >= texture_entry.size() || face_bits >= 32) return std::nullopt;
                value = std::to_integer<std::uint8_t>(texture_entry[texture_position++]);
                faces = (faces << 7) | (value & 0x7f);
                face_bits += 7;
            } while (value & 0x80);
            if (faces == 0) break;
            if (texture_position + 16 > texture_entry.size()) return std::nullopt;
            Uuid texture_id;
            std::copy_n(texture_entry.begin() + texture_position, 16, texture_id.begin());
            texture_position += 16;
            for (unsigned face = 0; face < 32; ++face)
                if (faces & (std::uint32_t{1} << face)) result.texture_ids[face] = texture_id;
        }
    }
    return result;
}

std::optional<AgentAnimation> decode_agent_animation(std::span<const std::byte> payload) {
    constexpr std::size_t fixed_size = 34;
    constexpr std::size_t animation_size = 17;
    if (payload.size() < fixed_size || payload[0] != std::byte{5}) return std::nullopt;
    AgentAnimation result;
    std::copy_n(payload.begin() + 1, 16, result.agent_id.begin());
    std::copy_n(payload.begin() + 17, 16, result.session_id.begin());
    const auto count = std::to_integer<std::size_t>(payload[33]);
    auto position = fixed_size;
    if (position + count * animation_size + 1 > payload.size()) return std::nullopt;
    result.animations.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        AgentAnimationEntry entry;
        std::copy_n(payload.begin() + position, 16, entry.animation_id.begin());
        entry.start = payload[position + 16] != std::byte{};
        result.animations.push_back(entry);
        position += animation_size;
    }
    const auto physical_count = std::to_integer<std::size_t>(payload[position++]);
    for (std::size_t index = 0; index < physical_count; ++index) {
        if (position >= payload.size()) return std::nullopt;
        const auto size = std::to_integer<std::size_t>(payload[position++]);
        if (position + size > payload.size()) return std::nullopt;
        position += size;
    }
    if (position != payload.size()) return std::nullopt;
    return result;
}

std::vector<std::byte> encode_avatar_animation(const AvatarAnimation& message) {
    if (message.animations.empty() || message.animations.size() > 255) return {};
    std::vector<std::byte> output{std::byte{20}};
    append_uuid(output, message.sender_id);
    output.push_back(static_cast<std::byte>(message.animations.size()));
    for (const auto& animation : message.animations) {
        append_uuid(output, animation.animation_id);
        append_le_u32(output, static_cast<std::uint32_t>(animation.sequence));
    }
    output.push_back(static_cast<std::byte>(message.animations.size()));
    for (const auto& animation : message.animations) append_uuid(output, animation.source_id);
    output.push_back(std::byte{}); // no physical avatar events
    return output;
}

std::optional<AssetUploadRequest> decode_asset_upload_request(std::span<const std::byte> payload) {
    constexpr std::array<std::byte, 4> message_id{
        std::byte{0xff}, std::byte{0xff}, std::byte{0x01}, std::byte{0x4d}};
    if (payload.size() < 25 ||
        !std::equal(message_id.begin(), message_id.end(), payload.begin()))
        return std::nullopt;
    const auto size = std::to_integer<std::size_t>(payload[23]) |
                      (std::to_integer<std::size_t>(payload[24]) << 8);
    if (payload.size() != 25 + size) return std::nullopt;
    AssetUploadRequest result;
    std::copy_n(payload.begin() + 4, 16, result.transaction_id.begin());
    result.asset_type = static_cast<std::int8_t>(std::to_integer<std::uint8_t>(payload[20]));
    result.temporary = payload[21] != std::byte{};
    result.store_local = payload[22] != std::byte{};
    result.data.assign(payload.begin() + 25, payload.end());
    return result;
}

std::vector<std::byte> encode_asset_upload_complete(const Uuid& asset_id,
                                                    std::int8_t asset_type, bool success) {
    std::vector<std::byte> output{
        std::byte{0xff}, std::byte{0xff}, std::byte{0x01}, std::byte{0x4e}};
    append_uuid(output, asset_id);
    output.push_back(static_cast<std::byte>(asset_type));
    output.push_back(success ? std::byte{1} : std::byte{});
    return output;
}

std::optional<UpdateInventoryAsset> decode_update_inventory_asset(
    std::span<const std::byte> payload) {
    constexpr std::array<std::byte, 4> message_id{
        std::byte{0xff}, std::byte{0xff}, std::byte{0x01}, std::byte{0x0a}};
    constexpr std::size_t block_offset = 53;
    constexpr std::size_t transaction_offset = block_offset + 105;
    constexpr std::size_t string_offset = block_offset + 132;
    if (payload.size() < string_offset + 10 ||
        !std::equal(message_id.begin(), message_id.end(), payload.begin()) ||
        payload[52] != std::byte{1})
        return std::nullopt;
    auto position = string_offset;
    for (int field = 0; field < 2; ++field) {
        if (position >= payload.size()) return std::nullopt;
        const auto size = std::to_integer<std::size_t>(payload[position++]);
        if (size == 0 || position + size > payload.size() ||
            payload[position + size - 1] != std::byte{})
            return std::nullopt;
        position += size;
    }
    if (position + 8 != payload.size()) return std::nullopt;
    UpdateInventoryAsset result;
    std::copy_n(payload.begin() + 4, 16, result.agent_id.begin());
    std::copy_n(payload.begin() + 20, 16, result.session_id.begin());
    std::copy_n(payload.begin() + block_offset, 16, result.item_id.begin());
    std::copy_n(payload.begin() + transaction_offset, 16, result.transaction_id.begin());
    return result;
}

std::vector<std::byte> encode_request_xfer(std::uint64_t id, const Uuid& asset_id,
                                           std::int16_t asset_type) {
    std::vector<std::byte> output{
        std::byte{0xff}, std::byte{0xff}, std::byte{0x00}, std::byte{0x9c}};
    append_le_u64(output, id);
    if (!append_variable1(output, "")) return {};
    output.push_back(std::byte{}); // file path
    output.push_back(std::byte{}); // delete on completion
    output.push_back(std::byte{}); // use big packets
    append_uuid(output, asset_id);
    append_le_u16(output, static_cast<std::uint16_t>(asset_type));
    return output;
}

std::optional<XferPacket> decode_send_xfer_packet(std::span<const std::byte> payload) {
    if (payload.size() < 15 || payload[0] != std::byte{18}) return std::nullopt;
    const auto size = std::to_integer<std::size_t>(payload[13]) |
                      (std::to_integer<std::size_t>(payload[14]) << 8);
    if (payload.size() != 15 + size) return std::nullopt;
    XferPacket result;
    result.id = read_le_u32(payload, 1) |
                (static_cast<std::uint64_t>(read_le_u32(payload, 5)) << 32);
    result.packet = read_le_u32(payload, 9);
    result.data.assign(payload.begin() + 15, payload.end());
    return result;
}

std::vector<std::byte> encode_confirm_xfer_packet(std::uint64_t id, std::uint32_t packet) {
    std::vector<std::byte> output{std::byte{19}};
    append_le_u64(output, id);
    append_le_u32(output, packet);
    return output;
}

std::optional<RequestImage> decode_request_image(std::span<const std::byte> payload) {
    constexpr std::size_t header_size = 34;
    constexpr std::size_t block_size = 26;
    if (payload.size() < header_size || payload[0] != std::byte{8}) return std::nullopt;
    const auto count = std::to_integer<std::size_t>(payload[33]);
    if (count == 0 || payload.size() != header_size + count * block_size) return std::nullopt;
    RequestImage result;
    std::copy_n(payload.begin() + 1, 16, result.agent_id.begin());
    std::copy_n(payload.begin() + 17, 16, result.session_id.begin());
    result.requests.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        const auto offset = header_size + index * block_size;
        ImageRequestBlock block;
        std::copy_n(payload.begin() + offset, 16, block.image_id.begin());
        block.discard_level = static_cast<std::int8_t>(std::to_integer<std::uint8_t>(payload[offset + 16]));
        block.download_priority = read_f32(payload, offset + 17);
        block.packet = read_le_u32(payload, offset + 21);
        block.type = std::to_integer<std::uint8_t>(payload[offset + 25]);
        result.requests.push_back(block);
    }
    return result;
}

std::vector<std::vector<std::byte>> encode_image_transfer(
    const Uuid& image_id, std::span<const std::byte> content, std::uint32_t start_packet) {
    constexpr std::size_t first_packet_size = 600;
    constexpr std::size_t image_packet_size = 1000;
    if (content.empty()) return {};
    const auto remaining = content.size() > first_packet_size ? content.size() - first_packet_size : 0;
    const auto packet_count = 1 + (remaining + image_packet_size - 1) / image_packet_size;
    if (packet_count > std::numeric_limits<std::uint16_t>::max() || start_packet >= packet_count) return {};
    std::vector<std::vector<std::byte>> output;
    if (start_packet == 0) {
        std::vector<std::byte> header{std::byte{9}};
        append_uuid(header, image_id);
        header.push_back(std::byte{2});
        append_le_u32(header, static_cast<std::uint32_t>(content.size()));
        append_le_u16(header, static_cast<std::uint16_t>(packet_count));
        const auto first_size = std::min(first_packet_size, content.size());
        append_binary(header, content.first(first_size), 2);
        output.push_back(std::move(header));
        start_packet = 1;
    }
    for (std::size_t packet = start_packet; packet < packet_count; ++packet) {
        const auto offset = first_packet_size + (packet - 1) * image_packet_size;
        const auto size = std::min(image_packet_size, content.size() - offset);
        std::vector<std::byte> payload{std::byte{10}};
        append_uuid(payload, image_id);
        append_le_u16(payload, static_cast<std::uint16_t>(packet));
        append_binary(payload, content.subspan(offset, size), 2);
        output.push_back(std::move(payload));
    }
    return output;
}

std::optional<AgentUpdate> decode_agent_update(std::span<const std::byte> payload) {
    if (payload.size() != 115 || payload[0] != std::byte{4}) return std::nullopt;
    AgentUpdate result;
    std::copy_n(payload.begin() + 1, 16, result.agent_id.begin());
    std::copy_n(payload.begin() + 17, 16, result.session_id.begin());
    result.body_rotation = read_vector3(payload, 33);
    result.head_rotation = read_vector3(payload, 45);
    result.state = std::to_integer<std::uint8_t>(payload[57]);
    result.camera_center = read_vector3(payload, 58);
    result.camera_at = read_vector3(payload, 70);
    result.camera_left = read_vector3(payload, 82);
    result.camera_up = read_vector3(payload, 94);
    result.draw_distance = read_f32(payload, 106);
    result.control_flags = read_le_u32(payload, 110);
    result.flags = std::to_integer<std::uint8_t>(payload[114]);
    return result;
}

std::optional<ChatFromViewer> decode_chat_from_viewer(std::span<const std::byte> payload) {
    if (payload.size() < 43 || !std::equal(chat_from_viewer_id.begin(), chat_from_viewer_id.end(), payload.begin()))
        return std::nullopt;
    ChatFromViewer result;
    std::copy_n(payload.begin() + 4, 16, result.agent_id.begin());
    std::copy_n(payload.begin() + 20, 16, result.session_id.begin());
    const auto message = read_variable2(payload, 36);
    if (!message || message->second + 5 != payload.size()) return std::nullopt;
    result.message = message->first;
    result.type = std::to_integer<std::uint8_t>(payload[message->second]);
    result.channel = static_cast<std::int32_t>(read_le_u32(payload, message->second + 1));
    return result;
}

std::vector<std::byte> encode_chat_from_simulator(const ChatFromSimulator& message) {
    std::vector<std::byte> output(chat_from_simulator_id.begin(), chat_from_simulator_id.end());
    if (!append_variable1(output, message.from_name)) return {};
    append_uuid(output, message.source_id);
    append_uuid(output, message.owner_id);
    output.push_back(static_cast<std::byte>(message.source_type));
    output.push_back(static_cast<std::byte>(message.chat_type));
    output.push_back(static_cast<std::byte>(message.audible));
    for (const auto value : message.position) append_f32(output, value);
    if (!append_variable2(output, message.message)) return {};
    return output;
}

std::vector<std::byte> encode_flat_terrain(std::span<const TerrainPatch> patches, float height) {
    if (patches.empty() || patches.size() > 32 || !std::isfinite(height)) return {};
    BitWriter bits;
    bits.write_byte(0x08); bits.write_byte(0x01); // stride 264, little endian
    bits.write_byte(16); bits.write_byte(0x4c); // 16x16 land layer
    float offset = height - 0.5F;
    std::uint32_t offset_bits{};
    std::memcpy(&offset_bits, &offset, sizeof(offset_bits));
    for (const auto patch : patches) {
        if (patch.x >= 16 || patch.y >= 16) return {};
        bits.write_byte(0x84); // prequant 10, six-bit coefficient words
        bits.write_byte(static_cast<std::uint8_t>(offset_bits));
        bits.write_byte(static_cast<std::uint8_t>(offset_bits >> 8));
        bits.write_byte(static_cast<std::uint8_t>(offset_bits >> 16));
        bits.write_byte(static_cast<std::uint8_t>(offset_bits >> 24));
        bits.write_byte(1); bits.write_byte(0); // range 1
        // LLBitPack serializes a little-endian integer one byte at a time, so
        // this 10-bit field is its low byte followed by its upper two bits.
        const auto patch_id = (static_cast<std::uint32_t>(patch.x) << 5) | patch.y;
        bits.write_byte(static_cast<std::uint8_t>(patch_id));
        bits.write(patch_id >> 8, 2);
        bits.write(2, 2); // zero end-of-block
    }
    bits.write_byte(97); // end of patches
    const auto encoded = bits.finish();
    if (encoded.size() > 65535) return {};
    std::vector<std::byte> output{std::byte{11}, std::byte{0x4c}}; // LayerData, land
    const auto size = static_cast<std::uint16_t>(encoded.size());
    output.push_back(static_cast<std::byte>(size));
    output.push_back(static_cast<std::byte>(size >> 8));
    output.insert(output.end(), encoded.begin(), encoded.end());
    return output;
}

std::vector<std::byte> encode_terrain(std::span<const TerrainPatch> patches,
                                      std::span<const float> heightmap) {
    if (patches.empty() || patches.size() > 32 || heightmap.size() != 256 * 256 ||
        !std::all_of(heightmap.begin(), heightmap.end(), [](float height) { return std::isfinite(height); }))
        return {};
    BitWriter bits;
    bits.write_byte(0x08);
    bits.write_byte(0x01); // stride 264, little endian
    bits.write_byte(16);
    bits.write_byte(0x4c); // 16x16 land layer
    for (const auto patch : patches) {
        if (patch.x >= 16 || patch.y >= 16) return {};
        TerrainPatchHeader header;
        const auto coefficients = compress_terrain_patch(heightmap, patch.x, patch.y, header);
        const auto word_bits = write_terrain_patch_header(bits, header, coefficients);
        write_terrain_coefficients(bits, coefficients, word_bits);
    }
    bits.write_byte(97);
    const auto encoded = bits.finish();
    if (encoded.size() > 65535) return {};
    std::vector<std::byte> output{std::byte{11}, std::byte{0x4c}};
    const auto size = static_cast<std::uint16_t>(encoded.size());
    output.push_back(static_cast<std::byte>(size));
    output.push_back(static_cast<std::byte>(size >> 8));
    output.insert(output.end(), encoded.begin(), encoded.end());
    return output;
}

std::vector<std::byte> encode_static_object_update(std::uint64_t region_handle, const StaticObject& object) {
    std::vector<std::byte> output{std::byte{12}}; // high-frequency ObjectUpdate
    append_le_u64(output, region_handle);
    append_le_u16(output, 65535); // full time dilation
    output.push_back(std::byte{1}); // one ObjectData block
    append_le_u32(output, object.local_id);
    output.push_back(std::byte{}); // state
    append_uuid(output, object.id);
    append_le_u32(output, 0); // CRC
    output.push_back(static_cast<std::byte>(object.pcode));
    output.push_back(static_cast<std::byte>(object.material));
    output.push_back(std::byte{}); // click action
    for (const auto value : object.scale) append_f32(output, value);
    std::vector<std::byte> transform;
    for (const auto value : object.position) append_f32(transform, value);
    for (const auto value : object.velocity) append_f32(transform, value);
    for (const auto value : object.acceleration) append_f32(transform, value);
    for (const auto value : object.rotation) append_f32(transform, value);
    for (int index = 0; index < 3; ++index) append_f32(transform, 0.0F); // angular velocity
    if (!append_binary(output, transform, 1)) return {};
    append_le_u32(output, 0); // parent
    append_le_u32(output, object.update_flags);
    output.push_back(std::byte{16}); // straight path
    output.push_back(std::byte{1}); // square profile
    append_le_u16(output, 0); append_le_u16(output, 0); // path begin/end
    output.push_back(std::byte{100}); output.push_back(std::byte{100}); // path scale
    for (int index = 0; index < 7; ++index) output.push_back(std::byte{}); // shear through taper
    output.push_back(std::byte{}); // one path revolution: (value * 0.015) + 1.0
    output.push_back(std::byte{}); // skew
    append_le_u16(output, 0); append_le_u16(output, 0); append_le_u16(output, 0); // profile
    if (!append_binary(output, {}, 2) || !append_binary(output, {}, 1) ||
        !append_binary(output, {}, 2)) return {}; // texture, animation, name/value
    const std::array<std::byte, 1> prim_count{std::byte{1}};
    if (!append_binary(output, prim_count, 2) || !append_binary(output, {}, 1)) return {}; // data, text
    output.insert(output.end(), 4, std::byte{}); // text color
    if (!append_binary(output, {}, 1) || !append_binary(output, {}, 1)) return {}; // media, particles
    const std::array<std::byte, 1> no_extra_params{std::byte{0}};
    if (!append_binary(output, no_extra_params, 1)) return {};
    Uuid zero{};
    append_uuid(output, zero); append_uuid(output, object.owner_id); // sound and owner
    append_f32(output, 0.0F); output.push_back(std::byte{}); append_f32(output, 0.0F);
    output.push_back(std::byte{}); // joint type
    for (int index = 0; index < 6; ++index) append_f32(output, 0.0F);
    return output;
}

std::vector<std::byte> encode_avatar_object_update(std::uint64_t region_handle, std::uint32_t local_id,
                                                   const Uuid& agent_id,
                                                   std::array<float, 3> position,
                                                   std::array<float, 3> velocity,
                                                   std::array<float, 3> rotation) {
    StaticObject avatar;
    avatar.local_id = local_id;
    avatar.id = agent_id;
    avatar.owner_id = agent_id;
    avatar.pcode = 47; // avatar
    avatar.material = 4; // flesh
    avatar.position = position;
    avatar.velocity = velocity;
    avatar.rotation = rotation;
    avatar.scale = {0.45F, 0.60F, 1.90F};
    return encode_static_object_update(region_handle, avatar);
}

std::vector<std::byte> encode_packet_ack(std::span<const std::uint32_t> sequences) {
    if (sequences.empty() || sequences.size() > 255) return {};
    std::vector<std::byte> output(packet_ack_id.begin(), packet_ack_id.end());
    output.reserve(5 + sequences.size() * 4);
    output.push_back(static_cast<std::byte>(sequences.size()));
    for (const auto sequence : sequences) append_le_u32(output, sequence);
    return output;
}

std::optional<std::vector<std::uint32_t>> decode_packet_ack(std::span<const std::byte> payload) {
    if (payload.size() < 9 || !std::equal(packet_ack_id.begin(), packet_ack_id.end(), payload.begin()))
        return std::nullopt;
    const auto count = std::to_integer<std::size_t>(payload[4]);
    if (count == 0 || payload.size() != 5 + count * 4) return std::nullopt;
    std::vector<std::uint32_t> result;
    result.reserve(count);
    for (std::size_t offset = 5; offset < payload.size(); offset += 4) result.push_back(read_le_u32(payload, offset));
    return result;
}

std::vector<std::byte> encode_packet(const Packet& packet) {
    if (packet.extra_header.size() > 255 || packet.acknowledgements.size() > 255) return {};
    auto flags = packet.flags;
    if (!packet.acknowledgements.empty()) flags |= flag_appended_acks;
    std::vector<std::byte> output;
    output.reserve(6 + packet.extra_header.size() + packet.payload.size() + packet.acknowledgements.size() * 4 + 1);
    output.push_back(static_cast<std::byte>(flags));
    append_be_u32(output, packet.sequence);
    output.push_back(static_cast<std::byte>(packet.extra_header.size()));
    output.insert(output.end(), packet.extra_header.begin(), packet.extra_header.end());
    const auto encoded = (flags & flag_zero_coded) ? zero_encode(packet.payload) : packet.payload;
    output.insert(output.end(), encoded.begin(), encoded.end());
    for (const auto acknowledgement : packet.acknowledgements) append_be_u32(output, acknowledgement);
    if (!packet.acknowledgements.empty()) output.push_back(static_cast<std::byte>(packet.acknowledgements.size()));
    return output;
}

std::optional<Packet> decode_packet(std::span<const std::byte> datagram) {
    if (datagram.size() < 6) return std::nullopt;
    Packet packet;
    packet.flags = std::to_integer<std::uint8_t>(datagram[0]);
    packet.sequence = read_be_u32(datagram, 1);
    const auto extra_size = std::to_integer<std::size_t>(datagram[5]);
    if (6 + extra_size > datagram.size()) return std::nullopt;
    packet.extra_header.assign(datagram.begin() + 6, datagram.begin() + 6 + extra_size);
    std::size_t payload_end = datagram.size();
    if (packet.flags & flag_appended_acks) {
        if (payload_end < 7) return std::nullopt;
        const auto count = std::to_integer<std::size_t>(datagram[payload_end - 1]);
        if (count == 0 || count > (payload_end - 7) / 4) return std::nullopt;
        const auto ack_start = payload_end - 1 - count * 4;
        for (std::size_t offset = ack_start; offset < payload_end - 1; offset += 4)
            packet.acknowledgements.push_back(read_be_u32(datagram, offset));
        payload_end = ack_start;
    }
    const auto encoded = datagram.subspan(6 + extra_size, payload_end - 6 - extra_size);
    if (packet.flags & flag_zero_coded) {
        auto decoded = zero_decode(encoded);
        if (!decoded) return std::nullopt;
        packet.payload = std::move(*decoded);
    } else {
        packet.payload.assign(encoded.begin(), encoded.end());
    }
    return packet;
}

Circuit::Circuit(Clock::time_point now, double bytes_per_second, std::chrono::seconds idle_timeout)
    : last_activity_(now), token_time_(now), rate_(std::max(1.0, bytes_per_second)),
      capacity_(std::max(1200.0, bytes_per_second)), tokens_(capacity_), idle_timeout_(idle_timeout) {}

std::optional<std::vector<std::byte>> Circuit::send(std::vector<std::byte> payload, bool reliable,
                                                    Clock::time_point now, bool zero_coded) {
    Packet packet;
    packet.flags = (reliable ? flag_reliable : 0) | (zero_coded ? flag_zero_coded : 0);
    packet.sequence = next_sequence_++;
    packet.payload = std::move(payload);
    packet.acknowledgements = take_acks();
    auto datagram = encode_packet(packet);
    if (datagram.empty() || !consume(datagram.size(), now)) {
        queued_acks_.insert(queued_acks_.begin(), packet.acknowledgements.begin(), packet.acknowledgements.end());
        return std::nullopt;
    }
    if (reliable) pending_.emplace(packet.sequence, Pending{packet, now});
    last_activity_ = now;
    return datagram;
}

std::optional<Packet> Circuit::receive(std::span<const std::byte> datagram, Clock::time_point now) {
    auto packet = decode_packet(datagram);
    if (!packet) return std::nullopt;
    last_activity_ = now;
    for (const auto acknowledgement : packet->acknowledgements) pending_.erase(acknowledgement);
    if (const auto acknowledgements = decode_packet_ack(packet->payload))
        for (const auto acknowledgement : *acknowledgements) pending_.erase(acknowledgement);
    if (packet->flags & flag_reliable) {
        if (std::find(queued_acks_.begin(), queued_acks_.end(), packet->sequence) == queued_acks_.end())
            queued_acks_.push_back(packet->sequence);
        if (!received_reliable_.insert(packet->sequence).second) return std::nullopt;
        if (received_reliable_.size() > 4096) received_reliable_.clear();
    }
    return packet;
}

std::vector<std::vector<std::byte>> Circuit::poll(Clock::time_point now) {
    std::vector<std::vector<std::byte>> output;
    constexpr auto resend_after = std::chrono::milliseconds(500);
    for (auto& [sequence, pending] : pending_) {
        static_cast<void>(sequence);
        if (now - pending.sent_at < resend_after || pending.attempts >= 5) continue;
        auto resent = pending.packet;
        resent.flags |= flag_resent;
        resent.acknowledgements = take_acks();
        auto datagram = encode_packet(resent);
        if (!consume(datagram.size(), now)) {
            queued_acks_.insert(queued_acks_.begin(), resent.acknowledgements.begin(), resent.acknowledgements.end());
            continue;
        }
        pending.sent_at = now;
        ++pending.attempts;
        output.push_back(std::move(datagram));
    }
    if (output.empty() && !queued_acks_.empty()) {
        Packet ack;
        ack.sequence = next_sequence_++;
        const auto acknowledgements = take_acks();
        ack.payload = encode_packet_ack(acknowledgements);
        auto datagram = encode_packet(ack);
        if (consume(datagram.size(), now)) output.push_back(std::move(datagram));
        else queued_acks_.insert(queued_acks_.begin(), acknowledgements.begin(), acknowledgements.end());
    }
    return output;
}

bool Circuit::expired(Clock::time_point now) const { return now - last_activity_ > idle_timeout_; }

bool Circuit::consume(std::size_t bytes, Clock::time_point now) {
    const auto elapsed = std::chrono::duration<double>(now - token_time_).count();
    tokens_ = std::min(capacity_, tokens_ + std::max(0.0, elapsed) * rate_);
    token_time_ = now;
    if (tokens_ < static_cast<double>(bytes)) return false;
    tokens_ -= static_cast<double>(bytes);
    return true;
}

std::vector<std::uint32_t> Circuit::take_acks() {
    const auto count = std::min<std::size_t>(queued_acks_.size(), 255);
    std::vector<std::uint32_t> result(queued_acks_.begin(), queued_acks_.begin() + count);
    queued_acks_.erase(queued_acks_.begin(), queued_acks_.begin() + count);
    return result;
}

std::optional<Packet> CircuitRegistry::receive(std::string_view endpoint, std::span<const std::byte> datagram,
                                               Clock::time_point now) {
    auto found = circuits_.find(std::string(endpoint));
    if (found == circuits_.end()) {
        const auto packet = decode_packet(datagram);
        if (!packet || !(packet->flags & flag_reliable)) return std::nullopt;
        const auto requested = decode_use_circuit_code(packet->payload);
        if (!requested) return std::nullopt;
        bool authorized = false;
        try {
            authorized = authorizer_ && authorizer_(*requested);
        } catch (...) {
            return std::nullopt;
        }
        if (!authorized) return std::nullopt;
        for (const auto& [key, entry] : circuits_) {
            static_cast<void>(key);
            if (entry.identity.circuit_code == requested->circuit_code ||
                entry.identity.session_id == requested->session_id || entry.identity.agent_id == requested->agent_id)
                return std::nullopt;
        }
        found = circuits_.emplace(std::string(endpoint), Entry{*requested, Circuit(now)}).first;
    }
    return found->second.circuit.receive(datagram, now);
}

std::optional<std::vector<std::byte>> CircuitRegistry::send(std::string_view endpoint,
                                                            std::vector<std::byte> payload, bool reliable,
                                                            Clock::time_point now, bool zero_coded) {
    const auto found = circuits_.find(std::string(endpoint));
    if (found == circuits_.end()) return std::nullopt;
    return found->second.circuit.send(std::move(payload), reliable, now, zero_coded);
}

std::vector<OutboundDatagram> CircuitRegistry::poll(Clock::time_point now) {
    std::vector<OutboundDatagram> output;
    for (auto iterator = circuits_.begin(); iterator != circuits_.end();) {
        if (iterator->second.circuit.expired(now)) {
            iterator = circuits_.erase(iterator);
            continue;
        }
        auto datagrams = iterator->second.circuit.poll(now);
        for (auto& datagram : datagrams) output.push_back({iterator->first, std::move(datagram)});
        ++iterator;
    }
    return output;
}

const UseCircuitCode* CircuitRegistry::identity(std::string_view endpoint) const {
    const auto found = circuits_.find(std::string(endpoint));
    return found == circuits_.end() ? nullptr : &found->second.identity;
}

bool CircuitRegistry::remove(std::string_view endpoint) {
    return circuits_.erase(std::string(endpoint)) != 0;
}

} // namespace homeworldz::viewer
