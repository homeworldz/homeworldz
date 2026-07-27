#pragma once

// The region-session protocol layer (docs/CLIENT2.md, "A shared C++ protocol
// library for the region session"): envelope encoding, first-byte encoding
// discrimination, and the session state machine, carrying no transport. Both
// ends of the region session are C++; this is written to be consumable from
// the client core later, so it depends on nothing beyond the standard
// library.

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace homeworldz::session {

// The envelope version, shared with the grid channel's envelopes.
constexpr int envelope_version = 1;

struct Envelope {
    std::string type;
    int version{};
    std::string correlation_id;
    // The payload object's raw JSON text ("{...}"), empty when absent.
    std::string payload;
};

enum class ParseError {
    none,
    // The first byte is not '{': a different encoding, refused by name
    // (docs/CLIENT2.md, "Encoding: JSON on both channels").
    wrong_encoding,
    malformed,
};

std::optional<Envelope> parse_envelope(std::string_view text, ParseError& error);

// encode_envelope renders one envelope; payload_object must be a pre-encoded
// JSON object (or empty to omit the field).
std::string encode_envelope(std::string_view type, std::string_view correlation_id,
                            std::string_view payload_object);

// json_string renders a quoted, escaped JSON string.
std::string json_string(std::string_view value);

// json_field extracts a string field from a JSON object's raw text; empty
// when absent. Sufficient for the session protocol's flat payloads.
std::string json_field(std::string_view object, std::string_view name);

// json_number extracts a top-level numeric field; nullopt when absent or
// not a number.
std::optional<double> json_number(std::string_view object, std::string_view name);

// json_vector3 extracts a top-level [x,y,z] array field.
std::optional<std::array<float, 3>> json_vector3(std::string_view object, std::string_view name);

// json_object_field extracts a nested object field's raw text.
std::string json_object_field(std::string_view object, std::string_view name);

// SessionIdentity is who an authenticated session belongs to, as resolved by
// the grid from the region ticket.
struct SessionIdentity {
    std::string user_id;
    std::string userid;
    std::string display_name;
    std::string session_id;
};

// TicketValidator resolves a presented region ticket, or nothing when it is
// refused. The region implements this by asking the grid (the signing secret
// never reaches a region).
using TicketValidator = std::function<std::optional<SessionIdentity>(const std::string& token)>;

// Command is an embodiment request an authenticated session made
// (docs/CLIENT2-EMBODIMENT.md): parsed and validated at the protocol layer,
// executed by the host simulation, which owns the scene and replies on its
// own clock. Fields are meaningful per kind.
struct Command {
    enum class Kind { spawn, move, say, leave };
    Kind kind{};
    // spawn/move: requested draw distance; negative means "not carried" —
    // the host keeps its stored per-session value (never zero: zero means
    // no-filter to the interest check, which is what decision 6 forbids).
    double draw_distance{-1.0};
    // move fields, mirroring AvatarController::MovementInput.
    std::uint32_t controls{};
    std::array<float, 3> body_rotation{};
    bool has_camera{};
    std::array<float, 3> camera_center{};
    std::array<float, 3> camera_at{};
    std::array<float, 3> camera_left{};
    std::array<float, 3> camera_up{};
    // say.
    std::string message;
};

// SessionCore is one connection's protocol state machine, transport-free so
// it is testable and shareable: the transport feeds it inbound text and
// carries away what it says to send.
class SessionCore {
public:
    SessionCore(std::string region_name, TicketValidator validator);

    struct Result {
        std::vector<std::string> send;
        bool close{};
        std::string close_reason;
        // An embodiment command for the host simulation; the protocol layer
        // answered nothing yet (the host replies when it acts).
        std::optional<Command> command;
    };

    // handle_text processes one complete text message.
    Result handle_text(std::string_view text);

    // handle_binary refuses a binary frame: the encoding is self-describing
    // by first byte on text frames, and no binary encoding exists yet.
    Result handle_binary() const;

    bool established() const { return established_; }
    const SessionIdentity& identity() const { return identity_; }

private:
    Result refuse(std::string reason) const;

    std::string region_name_;
    TicketValidator validator_;
    bool established_{};
    SessionIdentity identity_;
};

} // namespace homeworldz::session
