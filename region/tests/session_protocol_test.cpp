#include "homeworldz/session_protocol.h"

#include <string>

using homeworldz::session::Envelope;
using homeworldz::session::ParseError;
using homeworldz::session::SessionCore;
using homeworldz::session::SessionIdentity;

int main() {
    // Encoding renders the envelope shape the grid channel established.
    const auto encoded = homeworldz::session::encode_envelope(
        "chat", "req-1", R"({"from":"Object","message":"hi \"you\""})");
    if (encoded != R"({"type":"chat","version":1,"correlationId":"req-1","payload":)"
                   R"({"from":"Object","message":"hi \"you\""}})")
        return 1;

    // Round trip: parse what was encoded.
    ParseError error{};
    const auto parsed = homeworldz::session::parse_envelope(encoded, error);
    if (!parsed || error != ParseError::none || parsed->type != "chat" ||
        parsed->version != 1 || parsed->correlation_id != "req-1" ||
        homeworldz::session::json_field(parsed->payload, "message") != "hi \"you\"")
        return 2;

    // First-byte discrimination: any other leading byte is a different
    // encoding, refused by name rather than parsed as JSON.
    if (homeworldz::session::parse_envelope("P\x02二进制", error) ||
        error != ParseError::wrong_encoding)
        return 3;
    if (homeworldz::session::parse_envelope("{\"version\":1}", error) ||
        error != ParseError::malformed)
        return 4;

    // Escapes render and parse both ways.
    if (homeworldz::session::json_string("a\nb\t\"c\"\\") != R"("a\nb\t\"c\"\\")") return 5;
    const auto tricky = homeworldz::session::parse_envelope(
        R"({"type":"auth","version":1,"payload":{"token":"with \"quotes\" and {braces}"}})", error);
    if (!tricky || homeworldz::session::json_field(tricky->payload, "token") !=
                       "with \"quotes\" and {braces}")
        return 6;

    // A surrogate pair decodes to one supplementary code point (proper
    // UTF-8, never CESU-8), and a lone surrogate is refused rather than
    // substituted.
    const auto emoji = homeworldz::session::parse_envelope(
        "{\"type\":\"chat\",\"version\":1,\"payload\":{\"message\":\"\\ud83d\\ude00 hi\"}}", error);
    if (!emoji || homeworldz::session::json_field(emoji->payload, "message") !=
                      "\xf0\x9f\x98\x80 hi")
        return 16;
    const auto lone = homeworldz::session::parse_envelope(
        R"({"type":"chat","version":1,"payload":{"message":"\ud83d oops"}})", error);
    if (!lone || !homeworldz::session::json_field(lone->payload, "message").empty()) return 17;
    if (homeworldz::session::parse_envelope(R"({"type":"\udc00","version":1})", error) ||
        error != ParseError::malformed)
        return 18;

    // A field named inside a nested payload must not satisfy a top-level
    // lookup: the envelope's own type wins.
    const auto nested = homeworldz::session::parse_envelope(
        R"({"payload":{"type":"impostor"},"type":"ping","version":1})", error);
    if (!nested || nested->type != "ping") return 7;

    // --- SessionCore: the connection state machine.
    const SessionIdentity jim{"efa3f54c-0000-4000-8000-000000000001", "jim.tarber", "Jim Tarber",
                              "bbbbbbbb-0000-4000-8000-000000000002"};
    const auto validator = [&](const std::string& token) -> std::optional<SessionIdentity> {
        if (token == "good-ticket") return jim;
        return std::nullopt;
    };

    // The first message must be auth; anything else closes.
    SessionCore impatient("Sandbox", validator);
    if (const auto result = impatient.handle_text(R"({"type":"ping","version":1})"); !result.close)
        return 8;

    // A refused ticket closes with the refusal named.
    SessionCore refused("Sandbox", validator);
    if (const auto result = refused.handle_text(
            R"({"type":"auth","version":1,"payload":{"token":"bad"}})");
        !result.close || result.close_reason.find("ticket") == std::string::npos)
        return 9;

    // The happy path: auth resolves, hello names the region and identity.
    SessionCore session("Sandbox", validator);
    const auto hello = session.handle_text(
        R"({"type":"auth","version":1,"payload":{"token":"good-ticket"}})");
    if (hello.close || hello.send.size() != 1 || !session.established() ||
        session.identity().userid != "jim.tarber")
        return 10;
    const auto greeting = homeworldz::session::parse_envelope(hello.send.front(), error);
    if (!greeting || greeting->type != "hello" ||
        homeworldz::session::json_field(greeting->payload, "region") != "Sandbox")
        return 11;

    // Ping answers pong carrying the correlation identifier.
    const auto pong = session.handle_text(
        R"({"type":"ping","version":1,"correlationId":"beat-7"})");
    if (pong.close || pong.send.size() != 1) return 12;
    const auto beat = homeworldz::session::parse_envelope(pong.send.front(), error);
    if (!beat || beat->type != "pong" || beat->correlation_id != "beat-7") return 13;

    // An unknown type earns an error envelope, not a close.
    const auto unknown = session.handle_text(R"({"type":"mystery","version":1})");
    if (unknown.close || unknown.send.size() != 1 ||
        unknown.send.front().find("unsupported_type") == std::string::npos)
        return 14;

    // Binary frames are refused: no binary encoding exists yet.
    if (const auto result = session.handle_binary(); !result.close) return 15;

    return 0;
}
