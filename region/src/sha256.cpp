#include "homeworldz/sha256.h"

#include <array>
#include <cstdint>
#include <vector>

namespace homeworldz::crypto {
namespace {

constexpr std::array<std::uint32_t, 64> constants{
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};

std::uint32_t rotate_right(std::uint32_t value, int count) {
    return (value >> count) | (value << (32 - count));
}

} // namespace

std::string sha256_hex(std::span<const std::byte> data) {
    std::vector<std::uint8_t> message;
    message.reserve(data.size() + 72);
    for (const auto value : data) message.push_back(std::to_integer<std::uint8_t>(value));
    const auto bit_length = static_cast<std::uint64_t>(message.size()) * 8;
    message.push_back(0x80);
    while (message.size() % 64 != 56) message.push_back(0);
    for (int shift = 56; shift >= 0; shift -= 8) message.push_back(static_cast<std::uint8_t>(bit_length >> shift));

    std::array<std::uint32_t, 8> hash{
        0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    for (std::size_t offset = 0; offset < message.size(); offset += 64) {
        std::array<std::uint32_t, 64> words{};
        for (std::size_t index = 0; index < 16; ++index) {
            const auto base = offset + index * 4;
            words[index] = (static_cast<std::uint32_t>(message[base]) << 24) |
                           (static_cast<std::uint32_t>(message[base + 1]) << 16) |
                           (static_cast<std::uint32_t>(message[base + 2]) << 8) | message[base + 3];
        }
        for (std::size_t index = 16; index < 64; ++index) {
            const auto s0 = rotate_right(words[index - 15], 7) ^ rotate_right(words[index - 15], 18) ^ (words[index - 15] >> 3);
            const auto s1 = rotate_right(words[index - 2], 17) ^ rotate_right(words[index - 2], 19) ^ (words[index - 2] >> 10);
            words[index] = words[index - 16] + s0 + words[index - 7] + s1;
        }
        auto a=hash[0], b=hash[1], c=hash[2], d=hash[3], e=hash[4], f=hash[5], g=hash[6], h=hash[7];
        for (std::size_t index = 0; index < 64; ++index) {
            const auto sum1 = rotate_right(e,6)^rotate_right(e,11)^rotate_right(e,25);
            const auto choose = (e&f)^((~e)&g);
            const auto temporary1 = h+sum1+choose+constants[index]+words[index];
            const auto sum0 = rotate_right(a,2)^rotate_right(a,13)^rotate_right(a,22);
            const auto majority = (a&b)^(a&c)^(b&c);
            const auto temporary2 = sum0+majority;
            h=g; g=f; f=e; e=d+temporary1; d=c; c=b; b=a; a=temporary1+temporary2;
        }
        hash[0]+=a; hash[1]+=b; hash[2]+=c; hash[3]+=d; hash[4]+=e; hash[5]+=f; hash[6]+=g; hash[7]+=h;
    }
    constexpr char hex[] = "0123456789abcdef";
    std::string result(64, '0');
    for (std::size_t index = 0; index < hash.size(); ++index) {
        for (int nibble = 0; nibble < 8; ++nibble) {
            result[index * 8 + nibble] = hex[(hash[index] >> ((7 - nibble) * 4)) & 0x0f];
        }
    }
    return result;
}

} // namespace homeworldz::crypto
