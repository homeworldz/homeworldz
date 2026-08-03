#include "homeworldz/texture_entry.h"

#include <algorithm>

namespace homeworldz::texture_entry {

namespace {

// Seven bits per byte, most significant group first, high bit continuing. Five
// groups is the most a 32-bit mask needs, so a longer run is malformed rather
// than merely large.
std::optional<std::pair<std::uint32_t, std::size_t>> read_face_bits(
    std::span<const std::byte> bytes, std::size_t position) {
    std::uint32_t mask = 0;
    for (std::size_t group = 0; group < 5; ++group) {
        if (position >= bytes.size()) return std::nullopt;
        const auto byte = std::to_integer<std::uint32_t>(bytes[position++]);
        mask = (mask << 7) | (byte & 0x7fu);
        if ((byte & 0x80u) == 0) return std::pair{mask, position};
    }
    return std::nullopt;
}

void append_face_bits(std::vector<std::byte>& out, std::uint32_t mask) {
    std::array<std::uint8_t, 5> groups{};
    std::size_t count = 0;
    do {
        groups[count++] = static_cast<std::uint8_t>(mask & 0x7fu);
        mask >>= 7;
    } while (mask != 0);
    for (std::size_t index = count; index > 1; --index)
        out.push_back(static_cast<std::byte>(groups[index - 1] | 0x80u));
    out.push_back(static_cast<std::byte>(groups[0]));
}

} // namespace

std::optional<Entry> parse(std::span<const std::byte> bytes) {
    Entry entry{};
    std::size_t position = 0;
    for (std::size_t field = 0; field < field_count; ++field) {
        const auto width = field_widths[field];
        auto& values = entry[field];
        values.fallback.assign(width, std::byte{});
        // Short is legal: the writer had nothing more to say. Everything from
        // here reads as absent.
        if (position + width > bytes.size()) continue;
        values.fallback.assign(bytes.begin() + static_cast<std::ptrdiff_t>(position),
                               bytes.begin() + static_cast<std::ptrdiff_t>(position + width));
        position += width;
        values.present = true;
        // Exceptions until the terminator. A stream that ends here is short
        // rather than malformed, and the field keeps its default.
        while (position < bytes.size()) {
            if (bytes[position] == std::byte{}) {
                ++position;
                break;
            }
            const auto bits = read_face_bits(bytes, position);
            if (!bits) return std::nullopt;
            const auto [mask, after] = *bits;
            if (after + width > bytes.size()) return std::nullopt;
            values.exceptions.emplace_back(
                mask, std::vector<std::byte>(
                          bytes.begin() + static_cast<std::ptrdiff_t>(after),
                          bytes.begin() + static_cast<std::ptrdiff_t>(after + width)));
            position = after + width;
        }
    }
    return entry;
}

std::vector<std::byte> encode(const Entry& entry) {
    // Trailing fields nobody set are left off, so an entry that came in short
    // and was not edited goes out the same length. Only the fields up to the
    // last one with content are written.
    std::size_t last = 0;
    for (std::size_t field = 0; field < field_count; ++field)
        if (entry[field].present || !entry[field].exceptions.empty()) last = field + 1;

    std::vector<std::byte> out;
    for (std::size_t field = 0; field < last; ++field) {
        const auto& values = entry[field];
        const auto width = field_widths[field];
        if (values.fallback.size() == width)
            out.insert(out.end(), values.fallback.begin(), values.fallback.end());
        else
            out.insert(out.end(), width, std::byte{});
        for (const auto& [mask, value] : values.exceptions) {
            if (mask == 0 || value.size() != width) continue;
            append_face_bits(out, mask);
            out.insert(out.end(), value.begin(), value.end());
        }
        out.push_back(std::byte{});
    }
    return out;
}

std::vector<std::byte> face_value(const Entry& entry, Field field, unsigned face) {
    const auto& values = entry[field];
    if (!values.present && values.exceptions.empty()) return {};
    if (face < max_faces) {
        const auto bit = 1u << face;
        // Later exceptions win, matching how a reader that applies them in order
        // would resolve an overlap.
        for (auto candidate = values.exceptions.rbegin();
             candidate != values.exceptions.rend(); ++candidate)
            if ((candidate->first & bit) != 0) return candidate->second;
    }
    return values.fallback;
}

void set_face(Entry& entry, Field field, unsigned face, std::span<const std::byte> value) {
    if (face >= max_faces) return;
    const auto width = field_widths[field];
    if (value.size() != width) return;
    auto& values = entry[field];
    if (values.fallback.size() != width) values.fallback.assign(width, std::byte{});
    values.present = true;
    const auto bit = 1u << face;

    // Whatever this face used to say, it no longer says it.
    for (auto& [mask, existing] : values.exceptions) mask &= ~bit;

    const bool matches_default = std::equal(value.begin(), value.end(), values.fallback.begin());
    if (!matches_default) {
        // Join an exception that already carries this exact value rather than
        // adding a second one saying the same thing.
        const auto same = std::find_if(
            values.exceptions.begin(), values.exceptions.end(), [&value](const auto& candidate) {
                return std::equal(candidate.second.begin(), candidate.second.end(), value.begin());
            });
        if (same != values.exceptions.end())
            same->first |= bit;
        else
            values.exceptions.emplace_back(
                bit, std::vector<std::byte>(value.begin(), value.end()));
    }

    // An exception covering no faces is noise.
    values.exceptions.erase(
        std::remove_if(values.exceptions.begin(), values.exceptions.end(),
                       [](const auto& candidate) { return candidate.first == 0; }),
        values.exceptions.end());
}

} // namespace homeworldz::texture_entry
