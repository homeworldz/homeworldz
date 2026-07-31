#pragma once

// LLSD's binary serialization, both directions, over the same Value model the
// XML reader produces.
//
// Needed because the RenderMaterials capability does not speak XML: a viewer
// sends and expects a single "Zipped" binary member holding zlib-deflated LLSD
// *binary*, so a region that can only read XML and only write hand-assembled
// XML cannot answer it at all. That is why viewer material assignments have
// never persisted here - there was no way for a viewer to register a material
// definition and be told its id.
//
// Unlike the XML side this writes as well as reads, because the reply is a
// nested structure carrying binary ids and cannot be assembled by hand the way
// a flat capability reply can.

#include "homeworldz/llsd_xml.h"

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace homeworldz::llsd {

// Parse one LLSD binary value. Nothing on malformed input, on a length that
// exceeds the buffer, or on nesting deeper than the limit - capability bodies
// are machine-written, so anything irregular is refused rather than repaired.
std::optional<Value> parse_binary(std::span<const std::byte> data);

// Serialize one value. Total ordering of map members follows the Value's own
// member order, which the parser preserves, so a parse-then-serialize of a
// well-formed document reproduces it byte for byte.
std::vector<std::byte> to_binary(const Value& value);

// zlib deflate and inflate, as the "Zipped" members carry. inflate refuses
// anything that would expand past the limit rather than trusting the stream.
std::vector<std::byte> deflate_bytes(std::span<const std::byte> data);
std::optional<std::vector<std::byte>> inflate_bytes(std::span<const std::byte> data,
                                                    std::size_t limit = 8u << 20);

} // namespace homeworldz::llsd
