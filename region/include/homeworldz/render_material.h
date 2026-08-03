#pragma once

// Legacy Blinn-Phong materials: the normal and specular maps a viewer assigns
// through its build tool, carried by the RenderMaterials capability.
//
// The gap this closes: the region served no such capability, so a viewer had
// nowhere to register a material definition and no id to put on a face. Every
// materials edit was therefore discarded the moment it was made, silently,
// because from the viewer's side nothing failed - it simply never got an id
// back to store (found live 2026-07-29).
//
// **The field names below are not verified against a viewer.** They are this
// project's reading of the format, and a round-trip test would only prove that
// this file agrees with itself. So the capability logs the keys of every
// definition a viewer actually sends, and the first real edit will either
// confirm these names or name the ones to use. Until that happens, treat wire
// compatibility as unproven; the storage, the identity, and the persistence
// below are testable on their own and are tested.

#include "homeworldz/llsd_xml.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace homeworldz::material {

// Offsets, repeats and rotations travel as integers scaled by this factor
// rather than as reals, so a definition is exactly comparable and its identity
// is stable. Values are kept in wire form for that reason: converting to float
// and back would make two identical materials hash differently.
inline constexpr int wire_scale = 10000;

struct RenderMaterial {
    std::string normal_map;    // UUID, empty for none
    std::int32_t normal_offset_x{};
    std::int32_t normal_offset_y{};
    std::int32_t normal_repeat_x{wire_scale};
    std::int32_t normal_repeat_y{wire_scale};
    std::int32_t normal_rotation{};

    std::string specular_map;  // UUID, empty for none
    std::int32_t specular_offset_x{};
    std::int32_t specular_offset_y{};
    std::int32_t specular_repeat_x{wire_scale};
    std::int32_t specular_repeat_y{wire_scale};
    std::int32_t specular_rotation{};

    std::array<std::uint8_t, 4> specular_colour{255, 255, 255, 255};
    std::uint8_t specular_exponent{};
    std::uint8_t environment_intensity{};
    std::uint8_t alpha_mask_cutoff{};
    std::uint8_t diffuse_alpha_mode{};

    bool operator==(const RenderMaterial&) const = default;
};

// A material's identity is the hash of its own definition, so two viewers
// assigning the same maps and settings converge on one id and one stored record
// - the same content-addressing the blob layer uses (ADR 0027). It also means
// an id cannot disagree with what it names.
using MaterialId = std::array<std::byte, 16>;
MaterialId identify(const RenderMaterial& material);
std::string format_id(const MaterialId& id);

// LLSD conversion. from_llsd reports the keys it did not recognise rather than
// dropping them silently, because an unrecognised key is the most likely way
// the reading of this format above turns out to be wrong.
struct ParsedMaterial {
    RenderMaterial material;
    std::vector<std::string> unknown_keys;
    bool ok{};
};
ParsedMaterial from_llsd(const llsd::Value& value);
llsd::Value to_llsd(const RenderMaterial& material);

// Whether this map carries at least one field a material is made of.
//
// The envelope a viewer sends is not a bare definition — Firestorm wraps them
// in a "FullMaterialsPerFace" array of per-face entries (observed on the wire
// 2026-07-31) — and guessing the wrapper's shape is what made the region parse
// the envelope *as* a material and register an all-default one while reporting
// success. So definitions are found by what they contain rather than by where
// they sit, which is correct for any wrapper without knowing it.
bool looks_like_material(const llsd::Value& value);

// Every definition anywhere in a document, in document order. Descends maps and
// arrays and does not recurse into a map already identified as a definition.
std::vector<const llsd::Value*> find_materials(const llsd::Value& document);

// A compact rendering of a document's shape: keys, types, array lengths, and
// scalar values. For logging an unfamiliar body so the next question is answered
// by evidence rather than by another guess.
std::string describe(const llsd::Value& value, std::size_t limit = 2000);

} // namespace homeworldz::material
