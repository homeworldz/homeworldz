// The TextureEntry codec, and above all the edit that was missing.
//
// A viewer's material assignment names an object and a face and expects the
// server to write the resulting material id into that face. The region could
// build an entry and store one, not change a field of an existing one, so every
// face's material id stayed nil and a relog showed empty pickers. These tests
// are about the editing path being right, because it is about to be pointed at
// the operator's live prims.
#include "homeworldz/texture_entry.h"

#include <iostream>
#include <string>

using homeworldz::texture_entry::encode;
using homeworldz::texture_entry::Entry;
using homeworldz::texture_entry::face_value;
using homeworldz::texture_entry::field_widths;
using homeworldz::texture_entry::parse;
using homeworldz::texture_entry::set_face;

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

// A 16-byte value that is easy to recognise in a dump.
std::vector<std::byte> filled(int value) { return std::vector<std::byte>(16, static_cast<std::byte>(value)); }

// The region's own minimal builder writes exactly this shape: every field's
// default, each terminated, and no exceptions.
std::vector<std::byte> minimal_entry() {
    std::vector<std::byte> out;
    for (const auto width : field_widths) {
        out.insert(out.end(), width, std::byte{});
        out.push_back(std::byte{});
    }
    return out;
}

} // namespace

int main() {
    using F = homeworldz::texture_entry::Field;

    // --- A round trip of the canonical shape reproduces it exactly.
    {
        const auto original = minimal_entry();
        const auto entry = parse(original);
        check(entry.has_value(), "the canonical entry parses");
        if (entry) check(encode(*entry) == original, "and re-encodes byte for byte");
    }

    // --- A short stream is legal, and must not gain content it never had. The
    // region's builder omits the trailing terminator, and a viewer may stop
    // early too; inventing fields would change every stored entry's meaning.
    {
        // Just a default texture and its terminator: everything after is absent.
        std::vector<std::byte> short_entry = filled(0xAB);
        short_entry.push_back(std::byte{});
        const auto entry = parse(short_entry);
        check(entry.has_value(), "a short entry parses");
        if (entry) {
            check(face_value(*entry, F::texture, 0) == filled(0xAB),
                  "the field that was present reads back");
            check(face_value(*entry, F::material_id, 0).empty(),
                  "a field past the end reads as absent, not as zeros");
            check(encode(*entry) == short_entry, "and re-encodes to the same length");
        }
    }

    // --- Per-face exceptions, read back per face.
    {
        std::vector<std::byte> raw = filled(0x11);      // default texture
        raw.push_back(std::byte{0x05});                 // faces 0 and 2
        const auto face_texture = filled(0x22);
        raw.insert(raw.end(), face_texture.begin(), face_texture.end());
        raw.push_back(std::byte{});                     // end of texture exceptions
        const auto entry = parse(raw);
        check(entry.has_value(), "an entry with an exception parses");
        if (entry) {
            check(face_value(*entry, F::texture, 0) == filled(0x22), "face 0 takes the exception");
            check(face_value(*entry, F::texture, 2) == filled(0x22), "face 2 takes the exception");
            check(face_value(*entry, F::texture, 1) == filled(0x11), "face 1 takes the default");
            check(encode(*entry) == raw, "and the exception survives re-encoding");
        }
    }

    // --- A multi-byte face bitfield: seven bits per byte, most significant
    // group first, high bit continuing. Both two-group cases are asserted
    // because they differ by one bit in the leading group and getting that
    // backwards silently addresses the wrong face — the first draft of this test
    // had it wrong and the codec was right.
    //
    //   face 7  = bit 0x0080 -> groups 0x01,0x00 -> bytes 0x81 0x00
    //   face 8  = bit 0x0100 -> groups 0x02,0x00 -> bytes 0x82 0x00
    for (const auto& probe : {std::pair{0x81, 7u}, std::pair{0x82, 8u}}) {
        const auto [leading, face] = probe;
        std::vector<std::byte> raw = filled(0x00);
        raw.push_back(static_cast<std::byte>(leading));
        raw.push_back(std::byte{0x00});
        const auto value = filled(0x33);
        raw.insert(raw.end(), value.begin(), value.end());
        raw.push_back(std::byte{});
        const auto entry = parse(raw);
        check(entry.has_value(), "a two-group bitfield parses");
        if (entry) {
            check(face_value(*entry, F::texture, face) == filled(0x33),
                  "the named face resolves to the exception");
            check(face_value(*entry, F::texture, face - 1) != filled(0x33),
                  "its neighbour below does not");
            check(face_value(*entry, F::texture, face + 1) != filled(0x33),
                  "its neighbour above does not");
            check(encode(*entry) == raw, "and the two-group bitfield round-trips");
        }
    }

    // --- Malformed is refused rather than half-read.
    {
        std::vector<std::byte> truncated = filled(0x00);
        truncated.push_back(std::byte{0x01});   // an exception for face 0...
        truncated.push_back(std::byte{0xFF});   // ...whose value is cut short
        check(!parse(truncated), "an exception running past the end is refused");

        std::vector<std::byte> endless = filled(0x00);
        for (int i = 0; i < 6; ++i) endless.push_back(std::byte{0x80});  // never terminates
        check(!parse(endless), "a bitfield that never terminates is refused");
    }

    // --- The edit this file exists for: give one face a material id.
    {
        auto entry = parse(minimal_entry()).value();
        const auto material = filled(0x7E);
        set_face(entry, F::material_id, 3, material);

        check(face_value(entry, F::material_id, 3) == material, "face 3 carries the material");
        check(face_value(entry, F::material_id, 0) == filled(0x00),
              "faces not named keep the nil default");

        // Through the wire and back, since that is the only way it reaches a
        // viewer.
        const auto encoded = encode(entry);
        const auto reparsed = parse(encoded);
        check(reparsed.has_value(), "the edited entry parses");
        if (reparsed)
            check(face_value(*reparsed, F::material_id, 3) == material,
                  "and the material id survives the round trip");
    }

    // --- Several faces sharing one material share one exception, because the
    // viewer sends the same definition once per face and six copies of an
    // identical exception is six times the bytes for no information.
    {
        auto entry = parse(minimal_entry()).value();
        const auto material = filled(0x5A);
        for (unsigned face = 0; face < 6; ++face) set_face(entry, F::material_id, face, material);
        check(entry[F::material_id].exceptions.size() == 1,
              "six faces with one material make one exception");
        if (entry[F::material_id].exceptions.size() == 1)
            check(entry[F::material_id].exceptions[0].first == 0x3F,
                  "covering exactly faces 0 through 5");
        for (unsigned face = 0; face < 6; ++face)
            check(face_value(entry, F::material_id, face) == material,
                  "each of the six faces resolves to it");
        check(face_value(entry, F::material_id, 6) == filled(0x00), "face 6 is untouched");
    }

    // --- Clearing a face: setting the default back removes its exception rather
    // than storing one that says what the default already says. This is the
    // "remove the material" path, which a viewer asks for with nil maps.
    {
        auto entry = parse(minimal_entry()).value();
        const auto material = filled(0x42);
        set_face(entry, F::material_id, 0, material);
        set_face(entry, F::material_id, 1, material);
        check(entry[F::material_id].exceptions.size() == 1, "two faces, one exception");
        set_face(entry, F::material_id, 0, filled(0x00));
        check(face_value(entry, F::material_id, 0) == filled(0x00), "face 0 is cleared");
        check(face_value(entry, F::material_id, 1) == material, "face 1 is untouched");
        set_face(entry, F::material_id, 1, filled(0x00));
        check(entry[F::material_id].exceptions.empty(),
              "clearing the last face leaves no exception behind");
    }

    // --- Editing a material id must not disturb any other field. A prim whose
    // faces have different textures must still have them afterwards.
    {
        std::vector<std::byte> raw = filled(0x11);
        raw.push_back(std::byte{0x02});                  // face 1 has its own texture
        const auto other = filled(0x99);
        raw.insert(raw.end(), other.begin(), other.end());
        raw.push_back(std::byte{});
        for (std::size_t field = 1; field < field_widths.size(); ++field) {
            raw.insert(raw.end(), field_widths[field], std::byte{});
            raw.push_back(std::byte{});
        }
        auto entry = parse(raw).value();
        set_face(entry, F::material_id, 1, filled(0x77));
        check(face_value(entry, F::texture, 1) == other, "face 1 keeps its own texture");
        check(face_value(entry, F::texture, 0) == filled(0x11), "face 0 keeps the default");
        check(face_value(entry, F::material_id, 1) == filled(0x77), "and gains the material");
    }

    if (failures != 0) {
        std::cerr << failures << " texture entry check(s) failed\n";
        return 1;
    }
    std::cerr << "texture entry parse, encode and per-face edit OK\n";
    return 0;
}
