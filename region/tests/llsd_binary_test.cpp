// LLSD binary, both directions, and the zip the RenderMaterials capability
// wraps it in.
//
// The round-trip here is deliberately not the only check: a reader and writer
// written together will agree with each other whether or not either agrees with
// the format. So the first section asserts against byte sequences written out
// by hand from the format's own rules, and only then does the round-trip run.
#include "homeworldz/llsd_binary.h"

#include <iostream>
#include <string>
#include <vector>

using homeworldz::llsd::deflate_bytes;
using homeworldz::llsd::inflate_bytes;
using homeworldz::llsd::parse_binary;
using homeworldz::llsd::to_binary;
using homeworldz::llsd::Value;

namespace {

int failures = 0;

void check(bool condition, const std::string& what) {
    if (!condition) {
        std::cerr << "FAIL: " << what << '\n';
        ++failures;
    }
}

std::vector<std::byte> bytes_of(std::initializer_list<int> values) {
    std::vector<std::byte> out;
    for (const auto value : values) out.push_back(static_cast<std::byte>(value));
    return out;
}

} // namespace

int main() {
    // --- Read literal documents. Every count and length is 32-bit big-endian.
    {
        // 'i' then 0x0000002A
        const auto integer = parse_binary(bytes_of({'i', 0, 0, 0, 0x2A}));
        check(integer && integer->type == Value::Type::integer && integer->integer == 42,
              "integer reads big-endian");

        // A negative integer is two's complement in those same four bytes.
        const auto negative = parse_binary(bytes_of({'i', 0xFF, 0xFF, 0xFF, 0xFF}));
        check(negative && negative->integer == -1, "integer sign is preserved");

        const auto yes = parse_binary(bytes_of({'1'}));
        const auto no = parse_binary(bytes_of({'0'}));
        check(yes && yes->type == Value::Type::boolean && yes->boolean, "true");
        check(no && no->type == Value::Type::boolean && !no->boolean, "false");

        const auto undefined = parse_binary(bytes_of({'!'}));
        check(undefined && undefined->type == Value::Type::undefined, "undefined");

        // 's' then length 3 then "abc"
        const auto text = parse_binary(bytes_of({'s', 0, 0, 0, 3, 'a', 'b', 'c'}));
        check(text && text->type == Value::Type::string && text->text == "abc", "string");

        // 'b' then length 2 then two bytes
        const auto binary = parse_binary(bytes_of({'b', 0, 0, 0, 2, 0xDE, 0xAD}));
        check(binary && binary->type == Value::Type::binary && binary->binary.size() == 2 &&
                  binary->binary[0] == std::byte{0xDE},
              "binary");

        // 'u' then sixteen bytes, rendered in the canonical hyphenated form.
        const auto uuid = parse_binary(bytes_of({'u', 0x0d, 0x61, 0xd5, 0x64, 0x00, 0xae, 0x43,
                                                 0x88, 0xa8, 0x6a, 0xcc, 0x73, 0xb4, 0x80, 0xe2,
                                                 0x11}));
        check(uuid && uuid->type == Value::Type::uuid &&
                  uuid->text == "0d61d564-00ae-4388-a86a-cc73b480e211",
              "uuid formats hyphenated and lowercase");

        // '{' count 1, 'k' len 2 "id", 'i' 7, '}'
        const auto map = parse_binary(
            bytes_of({'{', 0, 0, 0, 1, 'k', 0, 0, 0, 2, 'i', 'd', 'i', 0, 0, 0, 7, '}'}));
        check(map && map->type == Value::Type::map && map->members.size() == 1 &&
                  map->find("id") != nullptr && map->find("id")->integer == 7,
              "map with one keyed member");

        // '[' count 2, '1', '0', ']'
        const auto array = parse_binary(bytes_of({'[', 0, 0, 0, 2, '1', '0', ']'}));
        check(array && array->type == Value::Type::array && array->elements.size() == 2 &&
                  array->elements[0].boolean && !array->elements[1].boolean,
              "array of two booleans");
    }

    // --- Refusals. A capability body is machine-written, so anything irregular
    // is rejected rather than repaired.
    {
        check(!parse_binary(bytes_of({})), "empty input");
        check(!parse_binary(bytes_of({'i', 0, 0})), "truncated integer");
        check(!parse_binary(bytes_of({'s', 0, 0, 0, 9, 'a', 'b'})), "string longer than the buffer");
        check(!parse_binary(bytes_of({'?'})), "unknown tag");
        // Count and terminator disagreeing means this is not a stream we
        // understand, even though the count alone would have sufficed.
        check(!parse_binary(bytes_of({'[', 0, 0, 0, 1, '1'})), "array missing its terminator");
        check(!parse_binary(bytes_of({'{', 0, 0, 0, 1, 'k', 0, 0, 0, 1, 'a', '1'})),
              "map missing its terminator");
        // A member without its 'k' tag desynchronizes everything after it.
        check(!parse_binary(bytes_of({'{', 0, 0, 0, 1, 's', 0, 0, 0, 1, 'a', '1', '}'})),
              "map member without a key tag");
        // Four bytes of count can claim four billion members.
        check(!parse_binary(bytes_of({'[', 0x7F, 0xFF, 0xFF, 0xFF, ']'})),
              "an absurd element count is refused before it is reserved");
    }

    // --- Write, checked against the same literal bytes rather than by
    // round-tripping through the reader.
    {
        Value integer;
        integer.type = Value::Type::integer;
        integer.integer = 42;
        check(to_binary(integer) == bytes_of({'i', 0, 0, 0, 0x2A}), "integer writes big-endian");

        Value text;
        text.type = Value::Type::string;
        text.text = "abc";
        check(to_binary(text) == bytes_of({'s', 0, 0, 0, 3, 'a', 'b', 'c'}), "string writes");

        Value uuid;
        uuid.type = Value::Type::uuid;
        uuid.text = "0d61d564-00ae-4388-a86a-cc73b480e211";
        const auto written = to_binary(uuid);
        check(written.size() == 17 && written[0] == std::byte{'u'} &&
                  written[1] == std::byte{0x0d} && written[16] == std::byte{0x11},
              "uuid writes sixteen bytes, hyphens dropped");

        // A malformed id must not write a short field, which would corrupt every
        // value after it; it writes the nil UUID instead.
        Value broken;
        broken.type = Value::Type::uuid;
        broken.text = "not-a-uuid";
        const auto broken_bytes = to_binary(broken);
        check(broken_bytes.size() == 17, "a malformed uuid still writes a full-width field");
    }

    // --- Round-trip of a nested document shaped like a materials reply.
    {
        Value material;
        material.type = Value::Type::map;
        {
            Value normal;
            normal.type = Value::Type::uuid;
            normal.text = "26700e50-492d-4243-9513-1905e8109e2b";
            material.members.emplace_back("NormMap", normal);
            Value exponent;
            exponent.type = Value::Type::integer;
            exponent.integer = 128;
            material.members.emplace_back("SpecExp", exponent);
            Value offset;
            offset.type = Value::Type::real;
            offset.real = 0.25;
            material.members.emplace_back("NormOffsetX", offset);
            Value colour;
            colour.type = Value::Type::array;
            for (const int component : {255, 255, 255, 255}) {
                Value element;
                element.type = Value::Type::integer;
                element.integer = component;
                colour.elements.push_back(element);
            }
            material.members.emplace_back("SpecColor", colour);
        }
        Value entry;
        entry.type = Value::Type::map;
        {
            Value id;
            id.type = Value::Type::binary;
            id.binary.assign(16, std::byte{0xAB});
            entry.members.emplace_back("ID", id);
            entry.members.emplace_back("Material", material);
        }
        Value list;
        list.type = Value::Type::array;
        list.elements.push_back(entry);

        const auto encoded = to_binary(list);
        const auto decoded = parse_binary(encoded);
        check(decoded.has_value(), "nested document parses");
        if (decoded) {
            check(to_binary(*decoded) == encoded,
                  "parse then serialize reproduces the bytes exactly");
            const auto& back = decoded->elements.at(0);
            const auto* recovered = back.find("Material");
            check(recovered != nullptr && recovered->find("SpecExp")->integer == 128,
                  "integer survives nesting");
            check(recovered->find("NormOffsetX")->as_real() == 0.25, "real survives nesting");
            check(recovered->find("NormMap")->text == "26700e50-492d-4243-9513-1905e8109e2b",
                  "uuid survives nesting");
            check(back.find("ID")->binary.size() == 16, "binary id survives nesting");
        }
    }

    // --- The zip the "Zipped" member carries.
    {
        Value document;
        document.type = Value::Type::array;
        for (int index = 0; index < 64; ++index) {
            Value element;
            element.type = Value::Type::string;
            element.text = "a repetitive value that should compress well";
            document.elements.push_back(element);
        }
        const auto raw = to_binary(document);
        const auto zipped = deflate_bytes(raw);
        check(!zipped.empty() && zipped.size() < raw.size() / 2,
              "deflate actually compresses a repetitive document");
        const auto back = inflate_bytes(zipped);
        check(back && *back == raw, "inflate reproduces the deflated bytes");
        check(!inflate_bytes(bytes_of({0x00, 0x01, 0x02, 0x03})),
              "inflate refuses input that is not a zlib stream");
        check(!inflate_bytes(zipped, 16), "inflate refuses to expand past its limit");
        check(!inflate_bytes(bytes_of({})), "inflate refuses empty input");
    }

    if (failures != 0) {
        std::cerr << failures << " LLSD binary check(s) failed\n";
        return 1;
    }
    std::cerr << "LLSD binary read/write and zip OK\n";
    return 0;
}
