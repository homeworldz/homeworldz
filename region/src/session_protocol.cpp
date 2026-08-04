#include "homeworldz/session_protocol.h"

#include "homeworldz/avatar_controller.h"
#include "homeworldz/terrain_layers.h"
#include "homeworldz/physics.h"
#include "homeworldz/mesh_acceptance.h"

#include <charconv>
#include <span>

namespace homeworldz::session {
namespace {

// find_field locates "name": in an object's top level and returns the index
// of the value's first character, or npos. Quote- and depth-aware so field
// names inside nested payloads or string values do not match.
std::size_t find_field(std::string_view object, std::string_view name) {
    const auto marker = "\"" + std::string(name) + "\"";
    std::size_t depth = 0;
    bool quoted = false;
    bool escaped = false;
    for (std::size_t position = 0; position < object.size(); ++position) {
        const auto character = object[position];
        if (quoted) {
            if (escaped) escaped = false;
            else if (character == '\\') escaped = true;
            else if (character == '"') quoted = false;
            continue;
        }
        switch (character) {
        case '{': case '[':
            ++depth;
            continue;
        case '}': case ']':
            if (depth > 0) --depth;
            continue;
        case '"':
            if (depth == 1 && object.compare(position, marker.size(), marker) == 0) {
                auto cursor = position + marker.size();
                while (cursor < object.size() &&
                       (object[cursor] == ' ' || object[cursor] == '\t')) ++cursor;
                if (cursor < object.size() && object[cursor] == ':') {
                    ++cursor;
                    while (cursor < object.size() &&
                           (object[cursor] == ' ' || object[cursor] == '\t')) ++cursor;
                    return cursor;
                }
            }
            quoted = true;
            continue;
        default:
            continue;
        }
    }
    return std::string_view::npos;
}

// parse_string_at decodes a JSON string value beginning at position (which
// must point at the opening quote), handling the escapes the protocol emits.
std::optional<std::string> parse_string_at(std::string_view text, std::size_t position) {
    if (position >= text.size() || text[position] != '"') return std::nullopt;
    std::string value;
    for (auto cursor = position + 1; cursor < text.size(); ++cursor) {
        const auto character = text[cursor];
        if (character == '"') return value;
        if (character != '\\') {
            value.push_back(character);
            continue;
        }
        if (++cursor >= text.size()) return std::nullopt;
        switch (text[cursor]) {
        case '"': value.push_back('"'); break;
        case '\\': value.push_back('\\'); break;
        case '/': value.push_back('/'); break;
        case 'n': value.push_back('\n'); break;
        case 't': value.push_back('\t'); break;
        case 'r': value.push_back('\r'); break;
        case 'b': value.push_back('\b'); break;
        case 'f': value.push_back('\f'); break;
        case 'u': {
            if (cursor + 4 >= text.size()) return std::nullopt;
            unsigned code = 0;
            const auto begin = text.data() + cursor + 1;
            if (std::from_chars(begin, begin + 4, code, 16).ec != std::errc{}) return std::nullopt;
            cursor += 4;
            // Surrogate pairs name one supplementary code point and must be
            // decoded as one — encoding each half separately would emit
            // CESU-8, which is not UTF-8. A lone or unpaired surrogate is
            // refused rather than substituted: replacing it would silently
            // alter the string the sender wrote.
            if (code >= 0xdc00 && code <= 0xdfff) return std::nullopt;
            if (code >= 0xd800 && code <= 0xdbff) {
                if (cursor + 6 >= text.size() || text[cursor + 1] != '\\' ||
                    text[cursor + 2] != 'u')
                    return std::nullopt;
                unsigned low = 0;
                const auto low_begin = text.data() + cursor + 3;
                if (std::from_chars(low_begin, low_begin + 4, low, 16).ec != std::errc{} ||
                    low < 0xdc00 || low > 0xdfff)
                    return std::nullopt;
                code = 0x10000 + ((code - 0xd800) << 10) + (low - 0xdc00);
                cursor += 6;
            }
            if (code < 0x80) {
                value.push_back(static_cast<char>(code));
            } else if (code < 0x800) {
                value.push_back(static_cast<char>(0xc0 | (code >> 6)));
                value.push_back(static_cast<char>(0x80 | (code & 0x3f)));
            } else if (code < 0x10000) {
                value.push_back(static_cast<char>(0xe0 | (code >> 12)));
                value.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3f)));
                value.push_back(static_cast<char>(0x80 | (code & 0x3f)));
            } else {
                value.push_back(static_cast<char>(0xf0 | (code >> 18)));
                value.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3f)));
                value.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3f)));
                value.push_back(static_cast<char>(0x80 | (code & 0x3f)));
            }
            break;
        }
        default:
            return std::nullopt;
        }
    }
    return std::nullopt;
}

// object_extent returns the raw text of the JSON object beginning at
// position, or nullopt when unbalanced.
std::optional<std::string> object_extent(std::string_view text, std::size_t position) {
    if (position >= text.size() || text[position] != '{') return std::nullopt;
    std::size_t depth = 0;
    bool quoted = false;
    bool escaped = false;
    for (auto cursor = position; cursor < text.size(); ++cursor) {
        const auto character = text[cursor];
        if (quoted) {
            if (escaped) escaped = false;
            else if (character == '\\') escaped = true;
            else if (character == '"') quoted = false;
            continue;
        }
        if (character == '"') quoted = true;
        else if (character == '{') ++depth;
        else if (character == '}' && --depth == 0)
            return std::string(text.substr(position, cursor - position + 1));
    }
    return std::nullopt;
}

} // namespace

// A shortest-round-trip decimal for a JSON number: "4" not "4.000000", and
// "9.81" exactly, so the constants a client reads are the constants the
// controller computes with.
std::string json_number_text(double value) {
    std::array<char, 32> buffer{};
    const auto [end, error] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    if (error != std::errc{}) return "0";
    return std::string(buffer.data(), end);
}

std::string base64(std::span<const std::byte> bytes) {
    static constexpr char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((bytes.size() + 2) / 3 * 4);
    std::size_t index = 0;
    while (index + 2 < bytes.size()) {
        const auto a = static_cast<unsigned>(bytes[index]);
        const auto b = static_cast<unsigned>(bytes[index + 1]);
        const auto c = static_cast<unsigned>(bytes[index + 2]);
        out.push_back(alphabet[a >> 2]);
        out.push_back(alphabet[((a & 0x3u) << 4) | (b >> 4)]);
        out.push_back(alphabet[((b & 0xfu) << 2) | (c >> 6)]);
        out.push_back(alphabet[c & 0x3fu]);
        index += 3;
    }
    if (index + 1 == bytes.size()) {
        const auto a = static_cast<unsigned>(bytes[index]);
        out.push_back(alphabet[a >> 2]);
        out.push_back(alphabet[(a & 0x3u) << 4]);
        out += "==";
    } else if (index + 2 == bytes.size()) {
        const auto a = static_cast<unsigned>(bytes[index]);
        const auto b = static_cast<unsigned>(bytes[index + 1]);
        out.push_back(alphabet[a >> 2]);
        out.push_back(alphabet[((a & 0x3u) << 4) | (b >> 4)]);
        out.push_back(alphabet[(b & 0xfu) << 2]);
        out.push_back('=');
    }
    return out;
}

std::string json_string(std::string_view value) {
    std::string rendered;
    rendered.reserve(value.size() + 2);
    rendered.push_back('"');
    for (const auto character : value) {
        switch (character) {
        case '"': rendered += "\\\""; break;
        case '\\': rendered += "\\\\"; break;
        case '\n': rendered += "\\n"; break;
        case '\r': rendered += "\\r"; break;
        case '\t': rendered += "\\t"; break;
        default:
            if (static_cast<unsigned char>(character) < 0x20) {
                constexpr char hexadecimal[] = "0123456789abcdef";
                rendered += "\\u00";
                rendered.push_back(hexadecimal[(character >> 4) & 0x0f]);
                rendered.push_back(hexadecimal[character & 0x0f]);
            } else {
                rendered.push_back(character);
            }
        }
    }
    rendered.push_back('"');
    return rendered;
}

std::string json_field(std::string_view object, std::string_view name) {
    const auto position = find_field(object, name);
    if (position == std::string_view::npos) return {};
    return parse_string_at(object, position).value_or(std::string{});
}

std::optional<double> json_number(std::string_view object, std::string_view name) {
    const auto position = find_field(object, name);
    if (position == std::string_view::npos) return std::nullopt;
    double value{};
    const auto begin = object.data() + position;
    const auto result = std::from_chars(begin, object.data() + object.size(), value);
    if (result.ec != std::errc{} || result.ptr == begin) return std::nullopt;
    return value;
}

std::optional<std::array<float, 3>> json_vector3(std::string_view object, std::string_view name) {
    const auto position = find_field(object, name);
    if (position == std::string_view::npos || position >= object.size() ||
        object[position] != '[')
        return std::nullopt;
    std::array<float, 3> value{};
    auto cursor = position + 1;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        while (cursor < object.size() && (object[cursor] == ' ' || object[cursor] == ','))
            ++cursor;
        const auto begin = object.data() + cursor;
        const auto result = std::from_chars(begin, object.data() + object.size(), value[axis]);
        if (result.ec != std::errc{} || result.ptr == begin) return std::nullopt;
        cursor = static_cast<std::size_t>(result.ptr - object.data());
    }
    while (cursor < object.size() && object[cursor] == ' ') ++cursor;
    if (cursor >= object.size() || object[cursor] != ']') return std::nullopt;
    return value;
}

std::string json_object_field(std::string_view object, std::string_view name) {
    const auto position = find_field(object, name);
    if (position == std::string_view::npos) return {};
    return object_extent(object, position).value_or(std::string{});
}

std::optional<Envelope> parse_envelope(std::string_view text, ParseError& error) {
    error = ParseError::none;
    if (text.empty() || text.front() != '{') {
        error = ParseError::wrong_encoding;
        return std::nullopt;
    }
    Envelope envelope;
    const auto type_at = find_field(text, "type");
    if (type_at == std::string_view::npos) {
        error = ParseError::malformed;
        return std::nullopt;
    }
    const auto type = parse_string_at(text, type_at);
    if (!type || type->empty()) {
        error = ParseError::malformed;
        return std::nullopt;
    }
    envelope.type = *type;

    if (const auto version_at = find_field(text, "version"); version_at != std::string_view::npos) {
        const auto begin = text.data() + version_at;
        if (std::from_chars(begin, text.data() + text.size(), envelope.version).ec != std::errc{}) {
            error = ParseError::malformed;
            return std::nullopt;
        }
    }
    if (const auto correlation_at = find_field(text, "correlationId");
        correlation_at != std::string_view::npos) {
        const auto correlation = parse_string_at(text, correlation_at);
        if (!correlation) {
            error = ParseError::malformed;
            return std::nullopt;
        }
        envelope.correlation_id = *correlation;
    }
    if (const auto payload_at = find_field(text, "payload"); payload_at != std::string_view::npos) {
        const auto payload = object_extent(text, payload_at);
        if (!payload) {
            error = ParseError::malformed;
            return std::nullopt;
        }
        envelope.payload = *payload;
    }
    return envelope;
}

std::string encode_envelope(std::string_view type, std::string_view correlation_id,
                            std::string_view payload_object) {
    std::string rendered = "{\"type\":" + json_string(type) +
                           ",\"version\":" + std::to_string(envelope_version);
    if (!correlation_id.empty()) rendered += ",\"correlationId\":" + json_string(correlation_id);
    if (!payload_object.empty()) rendered += ",\"payload\":" + std::string(payload_object);
    rendered.push_back('}');
    return rendered;
}

namespace {

// The layer ids and elevation corners as JSON lists. Written here rather than
// hand-assembled inline so the hello and the handshake cannot disagree about
// their order.
std::string layer_asset_list(const std::array<std::string, 4>& assets) {
    std::string out;
    for (const auto& asset : assets) {
        if (!out.empty()) out += ",";
        out += "\"" + std::string(asset) + "\"";
    }
    return out;
}

std::string corner_list(const std::array<float, 4>& values) {
    std::string out = "[";
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0) out += ",";
        out += json_number_text(values[index]);
    }
    return out + "]";
}

} // namespace


std::string water_json(double height) {
    return "{\"height\":" + json_number_text(height) +
           // Named so a client discovers the event rather than reading a
           // document, as terrain does. Water was fixed for the life of a region
           // process until the Region/Estate form could set it (2026-08-04), so
           // the greeting alone no longer suffices for a connection's lifetime.
           ",\"changedEvent\":\"waterChanged\"}";
}

std::string terrain_layers_json(const terrain::Settings& layers, double blend_metres) {
    return "{\"assets\":[" + layer_asset_list(layers.assets) +
           "],\"selectedBy\":\"elevation\""
           ",\"lowHeight\":" + corner_list(layers.low) +
           ",\"highHeight\":" + corner_list(layers.high) +
           ",\"corners\":\"sw,nw,se,ne\""
           // Which layer applies at a height, stated because the two bounds do
           // not imply it: they fix layer 1's ceiling and layer 4's floor, and
           // the division between 2 and 3 is the midpoint. Absolute metres,
           // both of them - the protocol's historical names say start height
           // and height range, and the viewer's own Region/Estate dialog says
           // low and high, which is what they are.
           ",\"selection\":\"1 below low; 2 low..mid; 3 mid..high;"
           " 4 above high; mid=(low+high)/2\""
           // The transition width, in metres, straddling each boundary
           // symmetrically. Advisory and honoured only by a client that shades
           // its own terrain: no legacy message carries a blend width, so a
           // viewer computes its own and this cannot change it. The mixing
           // curve stays unpublished - the region implements none, and an
           // approximate one is authoritative for this client while merely
           // approximate against a viewer on the same hill.
           ",\"blendMetres\":" + json_number_text(blend_metres) +
           // Whether this region still holds the shipped defaults. Was a
           // compile-time true; layers became per-region operator state on
           // 2026-08-04 and the field kept its meaning, which is why it was
           // worth publishing while it was still trivially true.
           ",\"gridWide\":" +
           std::string(layers.matches_defaults() ? "true" : "false") +
           // Named so a client discovers the event rather than reading a
           // document for it, exactly as terrain names terrainChanged. The
           // event carries this same block, so handling it is re-reading what
           // was already parsed once.
           ",\"changedEvent\":\"terrainLayersChanged\"}";
}




SessionCore::SessionCore(std::string region_name, TicketValidator validator,
                         std::size_t terrain_width, double walkable_slope_degrees,
                         double water_height, double terrain_blend_metres,
                         std::function<terrain::Settings()> terrain_layers,
                         std::function<std::uint64_t()> terrain_revision)
    : region_name_(std::move(region_name)), validator_(std::move(validator)),
      terrain_width_(terrain_width), walkable_slope_degrees_(walkable_slope_degrees),
      water_height_(water_height), terrain_blend_metres_(terrain_blend_metres),
      terrain_layers_(std::move(terrain_layers)),
      terrain_revision_(std::move(terrain_revision)) {}

SessionCore::Result SessionCore::refuse(std::string reason) const {
    Result result;
    result.close = true;
    result.close_reason = std::move(reason);
    return result;
}

SessionCore::Result SessionCore::handle_binary() const {
    return refuse("unsupported message encoding");
}

SessionCore::Result SessionCore::handle_text(std::string_view text) {
    ParseError error{};
    const auto envelope = parse_envelope(text, error);
    if (!envelope) {
        return refuse(error == ParseError::wrong_encoding ? "unsupported message encoding"
                                                          : "the message is not a valid envelope");
    }

    if (!established_) {
        // The mandatory first message is auth, exactly as on the grid channel.
        if (envelope->type != "auth") return refuse("the first message must be auth");
        const auto token = json_field(envelope->payload, "token");
        if (token.empty()) return refuse("auth requires a token");
        const auto resolved = validator_ ? validator_(token) : std::nullopt;
        if (!resolved) return refuse("the ticket is invalid, expired, or for another region");
        identity_ = *resolved;
        established_ = true;
        Result result;
        // The movement block publishes the region's authoritative movement
        // constants so a predicting client simulates this region rather than
        // guessing (docs/CLIENT2-EMBODIMENT.md). interestSweepMs is the
        // avatar-interest sweep period — the floor on how stale a remote
        // transform can be, which is what a client's extrapolation cap should
        // be derived from. Additive fields, per the payload contract.
        const auto layers = terrain_layers_ ? terrain_layers_() : terrain::Settings{};
        result.send.push_back(encode_envelope("hello", {},
            "{\"region\":" + json_string(region_name_) +
            ",\"identity\":{\"id\":" + json_string(identity_.user_id) +
            ",\"userid\":" + json_string(identity_.userid) +
            ",\"displayName\":" + json_string(identity_.display_name) + "}" +
            ",\"movement\":{\"walkSpeed\":" + json_number_text(homeworldz::viewer::avatar_walk_speed) +
            ",\"runSpeed\":" + json_number_text(homeworldz::viewer::avatar_fast_speed) +
            ",\"jumpVelocity\":" + json_number_text(homeworldz::viewer::avatar_jump_velocity) +
            ",\"gravity\":" + json_number_text(homeworldz::viewer::avatar_gravity) +
            "},\"interestSweepMs\":100" +
            // The avatar capsule and ground contract (client core request,
            // 2026-07-29): position is the capsule center, standing support
            // is ground + height/2, grounded within the tolerance above it.
            // The height itself is per-avatar and arrives in the spawned
            // reply. Same publication discipline as the movement block: the
            // controller header is the single definition.
            ",\"avatar\":{\"capsuleRadius\":" +
            json_number_text(homeworldz::viewer::avatar_capsule_radius) +
            ",\"supportOffsetFactor\":0.5" +
            ",\"groundedTolerance\":" +
            json_number_text(homeworldz::viewer::avatar_grounded_tolerance) +
            // Ground steeper than this never grounds the capsule: the avatar
            // is contact-held and sliding. This is strictly the grounded/
            // sliding boundary — NOT an exactness threshold for the support
            // rule, which is measured exact only on effectively flat ground
            // (≲3°) and deviates below flat arithmetic on real slopes even
            // while standing (docs/CLIENT2-EMBODIMENT.md, decision 4).
            ",\"walkableSlopeDegrees\":" +
            json_number_text(walkable_slope_degrees_) + "}" +
            // The ground itself: a heightmap fetched over HTTP with the same
            // region ticket this socket authenticated with. Heights are
            // float32 little-endian meters, row-major from y=0, one vertex
            // per meter (spacing 1), exact at integer coordinates. Between
            // vertices the surface the region collides against is each 1m
            // cell split into two planar triangles along the diagonal from
            // (x, y+1) to (x+1, y) — Jolt's heightfield triangulation, which
            // region movement samples by raycast every tick.
            ",\"terrain\":{\"path\":\"/session/terrain\"" +
            ",\"format\":\"heightmap-f32le\"" +
            ",\"width\":" + std::to_string(terrain_width_) +
            ",\"spacing\":1" +
            ",\"interpolation\":\"cell-triangles-diagonal-x,y+1-x+1,y\"" +
            // A fetch is a snapshot; this event names the dirty patches when
            // in-world editing changes the ground under a connected session.
            ",\"changedEvent\":\"terrainChanged\"" +
            // The revision this ground is at. It rides every terrainChanged
            // and is the heightmap's ETag, so notification can be coalesced
            // or dropped and a client still detects staleness - on a
            // reconnect or a crossing too, where a missed edit otherwise
            // survives unnoticed (client core, 2026-07-30).
            ",\"revision\":" +
            std::to_string(terrain_revision_ ? terrain_revision_() : 0) +
            ",\"ranges\":true" +
            // A terrainChanged may carry the changed heights outright, so an
            // edit costs no fetch. Same format as the map itself, patch-sized
            // and row-major within the patch, base64 in the JSON envelope, and
            // each patch states its own origin. Named here as the same media
            // type deliberately: one encoding of heights, not two (client core
            // caution, 2026-07-30).
            ",\"patchHeights\":{\"format\":\"heightmap-f32le\""
            ",\"encoding\":\"base64\",\"rowMajorWithinPatch\":true}" +
            // The ground's surface, not just its shape. Four textures selected
            // by elevation, lowest to highest, fetched from assets.base like
            // any other asset - they are canonical PNG since 2026-07-31, so a
            // client that refuses JPEG2000 can read them and a cache can hold
            // them for the life of the id.
            //
            // The block's own fields are documented on terrain_layers_json,
            // which builds it for both the greeting and terrainLayersChanged.
            // Per region since 2026-08-04: an operator sets them from the
            // viewer's Region/Estate -> Terrain tab, so a client must read them
            // per region and per connect rather than once for the grid.
            ",\"layers\":" + terrain_layers_json(layers, terrain_blend_metres_) + "}" +
            // The region's water: a height, not a surface. The plane is flat
            // and region-wide, in the same vertical datum as terrain heights,
            // and everything about how it is drawn is the client's business
            // (client core, 2026-07-30). Viewers learn the same number from
            // RegionHandshake; this is the session client's copy of it.
            ",\"water\":" + water_json(water_height_) +
            // Where canonical asset bytes come from: an asset id appended to
            // this base, on the same ticket. Named `base` rather than `path`
            // deliberately — `terrain.path` is a complete path and this is not,
            // and two keys of the same name behaving differently is the kind of
            // trap that gets read once and misread forever. The reply's
            // Content-Type states the format actually stored rather than one
            // implied by the id.
            ",\"assets\":{\"base\":\"/session/assets/\"}" +
            // The mesh acceptance gate, published so importing clients refuse
            // exactly what upload would refuse (ADR 0033: read, never encode).
            ",\"meshAcceptance\":" + homeworldz::mesh::acceptance_policy_json() + "}"));
        return result;
    }

    Result result;
    if (envelope->type == "ping") {
        result.send.push_back(encode_envelope("pong", envelope->correlation_id, {}));
        return result;
    }
    if (envelope->type == "spawn") {
        Command command;
        command.kind = Command::Kind::spawn;
        if (const auto requested = json_number(envelope->payload, "drawDistance"))
            command.draw_distance = *requested;
        result.command = std::move(command);
        return result;
    }
    if (envelope->type == "move") {
        Command command;
        command.kind = Command::Kind::move;
        if (const auto controls = json_number(envelope->payload, "controls"))
            command.controls = static_cast<std::uint32_t>(*controls);
        if (const auto rotation = json_vector3(envelope->payload, "bodyRotation"))
            command.body_rotation = *rotation;
        if (const auto requested = json_number(envelope->payload, "drawDistance"))
            command.draw_distance = *requested;
        if (const auto camera = json_object_field(envelope->payload, "camera"); !camera.empty()) {
            const auto center = json_vector3(camera, "center");
            const auto at = json_vector3(camera, "at");
            const auto left = json_vector3(camera, "left");
            const auto up = json_vector3(camera, "up");
            if (center && at && left && up) {
                command.has_camera = true;
                command.camera_center = *center;
                command.camera_at = *at;
                command.camera_left = *left;
                command.camera_up = *up;
            }
        }
        result.command = std::move(command);
        return result;
    }
    if (envelope->type == "say") {
        const auto message = json_field(envelope->payload, "message");
        // Characters, not bytes, matching the instant-message rule: count
        // UTF-8 code points as non-continuation bytes.
        std::size_t characters = 0;
        for (const auto byte : message)
            if ((static_cast<unsigned char>(byte) & 0xc0) != 0x80) ++characters;
        if (characters == 0 || characters > 2048) {
            result.send.push_back(encode_envelope("error", envelope->correlation_id,
                "{\"code\":\"invalid_message\",\"message\":\"message must be 1-2048 characters\",\"field\":\"message\"}"));
            return result;
        }
        Command command;
        command.kind = Command::Kind::say;
        command.message = message;
        result.command = std::move(command);
        return result;
    }
    if (envelope->type == "leave") {
        Command command;
        command.kind = Command::Kind::leave;
        result.command = std::move(command);
        return result;
    }
    result.send.push_back(encode_envelope("error", envelope->correlation_id,
        "{\"code\":\"unsupported_type\",\"message\":\"this message type is not supported\"}"));
    return result;
}

} // namespace homeworldz::session
