#pragma once

// Reading and editing a TextureEntry, not just building one.
//
// The region could assemble an entry from scratch and store one a viewer sent,
// but not change a field of an existing one. That gap is why a material
// assignment went nowhere: a viewer PUTs its definitions to RenderMaterials
// naming an object and a face, and the *server* is what writes the resulting
// material id into that face's entry. The definitions were stored and nothing
// referenced them, so every face's material id stayed nil and a relog showed
// empty Normal and Specular pickers (found live 2026-07-31).
//
// The format, in serialization order: eleven fields, each a fixed-width default
// followed by zero or more per-face exceptions, each exception a face bitfield
// then a value, the list terminated by a zero byte. The bitfield is seven bits
// per byte, most significant group first, high bit marking continuation.
//
// A truncated stream is not an error: a writer that had nothing to say about
// the trailing fields may simply stop, and the region's own builder does. Fields
// past the end read as absent and encode as their defaults.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace homeworldz::texture_entry {

enum Field : std::size_t {
    texture, colour, scale_s, scale_t, offset_s, offset_t, rotation,
    bump, media, glow, material_id, field_count
};

inline constexpr std::array<std::size_t, field_count> field_widths{
    16, 4, 4, 4, 2, 2, 2, 1, 1, 1, 16};

// The most faces a TextureEntry addresses; the bitfield is per-face and a prim
// has at most this many.
inline constexpr unsigned max_faces = 32;

struct FieldValues {
    std::vector<std::byte> fallback;
    std::vector<std::pair<std::uint32_t, std::vector<std::byte>>> exceptions;
    // False when the stream ended before this field. Preserved so a
    // parse-then-encode of an entry nobody edited does not invent content.
    bool present{};
};

using Entry = std::array<FieldValues, field_count>;

// Nothing on a stream that is malformed rather than merely short: a field whose
// exception runs past the end, or a bitfield that never terminates.
std::optional<Entry> parse(std::span<const std::byte> bytes);

// Every field through the last one with content, each with its terminator.
std::vector<std::byte> encode(const Entry& entry);

// The value a face actually resolves to: its exception if one covers it, else
// the field's default. Empty when the field is absent.
std::vector<std::byte> face_value(const Entry& entry, Field field, unsigned face);

// Give one face its own value for a field. A value equal to the default clears
// the face's exception rather than storing a redundant one, so an entry does not
// accumulate exceptions that say nothing.
void set_face(Entry& entry, Field field, unsigned face, std::span<const std::byte> value);

} // namespace homeworldz::texture_entry
