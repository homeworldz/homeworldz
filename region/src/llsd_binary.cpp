#include "homeworldz/llsd_binary.h"

#include <zlib.h>

#include <array>
#include <cstring>
#include <string>

namespace homeworldz::llsd {

namespace {

// Every count and length in LLSD binary is a 32-bit big-endian quantity, and
// every scalar that has one is tagged by a single ASCII byte.
constexpr std::byte tag_undefined{'!'};
constexpr std::byte tag_true{'1'};
constexpr std::byte tag_false{'0'};
constexpr std::byte tag_integer{'i'};
constexpr std::byte tag_real{'r'};
constexpr std::byte tag_uuid{'u'};
constexpr std::byte tag_binary{'b'};
constexpr std::byte tag_string{'s'};
constexpr std::byte tag_uri{'l'};
constexpr std::byte tag_date{'d'};
constexpr std::byte tag_map{'{'};
constexpr std::byte tag_map_end{'}'};
constexpr std::byte tag_key{'k'};
constexpr std::byte tag_array{'['};
constexpr std::byte tag_array_end{']'};

// A capability body has no business nesting deeply, and an unbounded recursion
// on attacker-shaped input is the one failure this parser must not have.
constexpr int maximum_depth = 32;
// A count field is four bytes, so a corrupt one can claim four billion members.
// Refuse before reserving anything.
constexpr std::uint32_t maximum_elements = 1u << 20;

class Reader {
public:
    explicit Reader(std::span<const std::byte> data) : data_(data) {}

    bool take(std::byte& out) {
        if (position_ >= data_.size()) return false;
        out = data_[position_++];
        return true;
    }

    bool take_u32(std::uint32_t& out) {
        if (position_ + 4 > data_.size()) return false;
        out = (std::to_integer<std::uint32_t>(data_[position_]) << 24) |
              (std::to_integer<std::uint32_t>(data_[position_ + 1]) << 16) |
              (std::to_integer<std::uint32_t>(data_[position_ + 2]) << 8) |
              std::to_integer<std::uint32_t>(data_[position_ + 3]);
        position_ += 4;
        return true;
    }

    bool take_u64(std::uint64_t& out) {
        std::uint32_t high{}, low{};
        if (!take_u32(high) || !take_u32(low)) return false;
        out = (static_cast<std::uint64_t>(high) << 32) | low;
        return true;
    }

    bool take_bytes(std::size_t count, std::span<const std::byte>& out) {
        if (count > data_.size() - position_) return false;
        out = data_.subspan(position_, count);
        position_ += count;
        return true;
    }

private:
    std::span<const std::byte> data_;
    std::size_t position_{};
};

bool parse_value(Reader& reader, Value& out, int depth);

bool parse_counted_string(Reader& reader, std::string& out) {
    std::uint32_t length{};
    if (!reader.take_u32(length)) return false;
    std::span<const std::byte> bytes;
    if (!reader.take_bytes(length, bytes)) return false;
    out.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    return true;
}

bool parse_value(Reader& reader, Value& out, int depth) {
    if (depth > maximum_depth) return false;
    std::byte tag{};
    if (!reader.take(tag)) return false;
    if (tag == tag_undefined) {
        out.type = Value::Type::undefined;
        return true;
    }
    if (tag == tag_true || tag == tag_false) {
        out.type = Value::Type::boolean;
        out.boolean = tag == tag_true;
        return true;
    }
    if (tag == tag_integer) {
        std::uint32_t raw{};
        if (!reader.take_u32(raw)) return false;
        out.type = Value::Type::integer;
        out.integer = static_cast<std::int32_t>(raw);
        return true;
    }
    if (tag == tag_real || tag == tag_date) {
        std::uint64_t raw{};
        if (!reader.take_u64(raw)) return false;
        double value{};
        std::memcpy(&value, &raw, sizeof(value));
        out.type = Value::Type::real;
        out.real = value;
        return true;
    }
    if (tag == tag_uuid) {
        std::span<const std::byte> bytes;
        if (!reader.take_bytes(16, bytes)) return false;
        static constexpr char digits[] = "0123456789abcdef";
        std::string text;
        text.reserve(36);
        for (std::size_t index = 0; index < 16; ++index) {
            if (index == 4 || index == 6 || index == 8 || index == 10) text.push_back('-');
            const auto byte = std::to_integer<unsigned>(bytes[index]);
            text.push_back(digits[(byte >> 4) & 0xf]);
            text.push_back(digits[byte & 0xf]);
        }
        out.type = Value::Type::uuid;
        out.text = std::move(text);
        return true;
    }
    if (tag == tag_string || tag == tag_uri) {
        std::string text;
        if (!parse_counted_string(reader, text)) return false;
        out.type = tag == tag_string ? Value::Type::string : Value::Type::uri;
        out.text = std::move(text);
        return true;
    }
    if (tag == tag_binary) {
        std::uint32_t length{};
        if (!reader.take_u32(length)) return false;
        std::span<const std::byte> bytes;
        if (!reader.take_bytes(length, bytes)) return false;
        out.type = Value::Type::binary;
        out.binary.assign(bytes.begin(), bytes.end());
        return true;
    }
    if (tag == tag_map) {
        std::uint32_t count{};
        if (!reader.take_u32(count) || count > maximum_elements) return false;
        out.type = Value::Type::map;
        out.members.reserve(count);
        for (std::uint32_t index = 0; index < count; ++index) {
            std::byte key_tag{};
            if (!reader.take(key_tag) || key_tag != tag_key) return false;
            std::string key;
            if (!parse_counted_string(reader, key)) return false;
            Value member;
            if (!parse_value(reader, member, depth + 1)) return false;
            out.members.emplace_back(std::move(key), std::move(member));
        }
        std::byte end{};
        // The terminator is redundant against the count, which is exactly why
        // it is worth checking: a stream where they disagree is not a stream we
        // understand.
        if (!reader.take(end) || end != tag_map_end) return false;
        return true;
    }
    if (tag == tag_array) {
        std::uint32_t count{};
        if (!reader.take_u32(count) || count > maximum_elements) return false;
        out.type = Value::Type::array;
        out.elements.reserve(count);
        for (std::uint32_t index = 0; index < count; ++index) {
            Value element;
            if (!parse_value(reader, element, depth + 1)) return false;
            out.elements.push_back(std::move(element));
        }
        std::byte end{};
        if (!reader.take(end) || end != tag_array_end) return false;
        return true;
    }
    return false;
}

void append_u32(std::vector<std::byte>& out, std::uint32_t value) {
    out.push_back(static_cast<std::byte>((value >> 24) & 0xff));
    out.push_back(static_cast<std::byte>((value >> 16) & 0xff));
    out.push_back(static_cast<std::byte>((value >> 8) & 0xff));
    out.push_back(static_cast<std::byte>(value & 0xff));
}

void append_counted(std::vector<std::byte>& out, std::string_view text) {
    append_u32(out, static_cast<std::uint32_t>(text.size()));
    for (const auto character : text) out.push_back(static_cast<std::byte>(character));
}

// "0123...-..." back to sixteen bytes. Anything that is not a UUID writes as
// the nil UUID rather than a malformed length, because a short id field would
// desynchronize every value after it.
void append_uuid(std::vector<std::byte>& out, std::string_view text) {
    std::array<std::byte, 16> bytes{};
    std::size_t written = 0;
    int high = -1;
    bool malformed = false;
    for (const auto character : text) {
        if (character == '-') continue;
        int digit = -1;
        if (character >= '0' && character <= '9') digit = character - '0';
        else if (character >= 'a' && character <= 'f') digit = character - 'a' + 10;
        else if (character >= 'A' && character <= 'F') digit = character - 'A' + 10;
        else { malformed = true; break; }
        if (high < 0) {
            high = digit;
        } else {
            if (written >= bytes.size()) { malformed = true; break; }
            bytes[written++] = static_cast<std::byte>((high << 4) | digit);
            high = -1;
        }
    }
    if (malformed || written != bytes.size() || high >= 0) bytes = {};
    out.insert(out.end(), bytes.begin(), bytes.end());
}

void write_value(const Value& value, std::vector<std::byte>& out) {
    switch (value.type) {
    case Value::Type::undefined:
        out.push_back(tag_undefined);
        return;
    case Value::Type::boolean:
        out.push_back(value.boolean ? tag_true : tag_false);
        return;
    case Value::Type::integer:
        out.push_back(tag_integer);
        append_u32(out, static_cast<std::uint32_t>(static_cast<std::int32_t>(value.integer)));
        return;
    case Value::Type::real: {
        out.push_back(tag_real);
        std::uint64_t raw{};
        const double real = value.real;
        std::memcpy(&raw, &real, sizeof(raw));
        append_u32(out, static_cast<std::uint32_t>(raw >> 32));
        append_u32(out, static_cast<std::uint32_t>(raw & 0xffffffffu));
        return;
    }
    case Value::Type::uuid:
        out.push_back(tag_uuid);
        append_uuid(out, value.text);
        return;
    case Value::Type::string:
        out.push_back(tag_string);
        append_counted(out, value.text);
        return;
    case Value::Type::uri:
        out.push_back(tag_uri);
        append_counted(out, value.text);
        return;
    case Value::Type::binary:
        out.push_back(tag_binary);
        append_u32(out, static_cast<std::uint32_t>(value.binary.size()));
        out.insert(out.end(), value.binary.begin(), value.binary.end());
        return;
    case Value::Type::map:
        out.push_back(tag_map);
        append_u32(out, static_cast<std::uint32_t>(value.members.size()));
        for (const auto& [key, member] : value.members) {
            out.push_back(tag_key);
            append_counted(out, key);
            write_value(member, out);
        }
        out.push_back(tag_map_end);
        return;
    case Value::Type::array:
        out.push_back(tag_array);
        append_u32(out, static_cast<std::uint32_t>(value.elements.size()));
        for (const auto& element : value.elements) write_value(element, out);
        out.push_back(tag_array_end);
        return;
    }
}

} // namespace

std::optional<Value> parse_binary(std::span<const std::byte> data) {
    Reader reader(data);
    Value value;
    if (!parse_value(reader, value, 0)) return std::nullopt;
    return value;
}

std::vector<std::byte> to_binary(const Value& value) {
    std::vector<std::byte> out;
    write_value(value, out);
    return out;
}

std::vector<std::byte> deflate_bytes(std::span<const std::byte> data) {
    z_stream stream{};
    if (deflateInit(&stream, Z_DEFAULT_COMPRESSION) != Z_OK) return {};
    std::vector<std::byte> out;
    out.resize(deflateBound(&stream, static_cast<uLong>(data.size())));
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<std::byte*>(data.data()));
    stream.avail_in = static_cast<uInt>(data.size());
    stream.next_out = reinterpret_cast<Bytef*>(out.data());
    stream.avail_out = static_cast<uInt>(out.size());
    const auto result = deflate(&stream, Z_FINISH);
    const auto produced = out.size() - stream.avail_out;
    deflateEnd(&stream);
    if (result != Z_STREAM_END) return {};
    out.resize(produced);
    return out;
}

std::optional<std::vector<std::byte>> inflate_bytes(std::span<const std::byte> data,
                                                    std::size_t limit) {
    if (data.empty()) return std::nullopt;
    z_stream stream{};
    if (inflateInit(&stream) != Z_OK) return std::nullopt;
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<std::byte*>(data.data()));
    stream.avail_in = static_cast<uInt>(data.size());
    std::vector<std::byte> out;
    std::array<std::byte, 16384> chunk{};
    int result = Z_OK;
    do {
        stream.next_out = reinterpret_cast<Bytef*>(chunk.data());
        stream.avail_out = static_cast<uInt>(chunk.size());
        result = inflate(&stream, Z_NO_FLUSH);
        if (result != Z_OK && result != Z_STREAM_END) {
            inflateEnd(&stream);
            return std::nullopt;
        }
        const auto produced = chunk.size() - stream.avail_out;
        // A compressed stream can name an enormous expansion in very few bytes,
        // so the ceiling is checked as it grows rather than trusted afterwards.
        if (out.size() + produced > limit) {
            inflateEnd(&stream);
            return std::nullopt;
        }
        out.insert(out.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(produced));
    } while (result != Z_STREAM_END);
    inflateEnd(&stream);
    return out;
}

} // namespace homeworldz::llsd
