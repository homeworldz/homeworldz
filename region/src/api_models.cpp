#include "homeworldz/api_models.h"

namespace homeworldz::api {

namespace {

// Length of the well-formed UTF-8 sequence starting at `at`, or 0 if what
// starts there is not one. Rejects the sequences that are the usual way invalid
// text slips through a naive validator: overlong encodings, UTF-16 surrogate
// halves, and anything above U+10FFFF.
std::size_t utf8_sequence_length(std::string_view value, std::size_t at) {
    const auto byte = static_cast<unsigned char>(value[at]);
    std::size_t length = 0;
    unsigned int code_point = 0;
    if (byte < 0x80) return 1;
    if ((byte & 0xe0) == 0xc0) { length = 2; code_point = byte & 0x1fu; }
    else if ((byte & 0xf0) == 0xe0) { length = 3; code_point = byte & 0x0fu; }
    else if ((byte & 0xf8) == 0xf0) { length = 4; code_point = byte & 0x07u; }
    else return 0;  // a continuation byte or 0xf8..0xff cannot start a sequence
    if (at + length > value.size()) return 0;
    for (std::size_t offset = 1; offset < length; ++offset) {
        const auto continuation = static_cast<unsigned char>(value[at + offset]);
        if ((continuation & 0xc0) != 0x80) return 0;
        code_point = (code_point << 6) | (continuation & 0x3fu);
    }
    if (length == 2 && code_point < 0x80) return 0;
    if (length == 3 && code_point < 0x800) return 0;
    if (length == 4 && code_point < 0x10000) return 0;
    if (code_point > 0x10ffff) return 0;
    if (code_point >= 0xd800 && code_point <= 0xdfff) return 0;
    return length;
}

} // namespace

// Bytes above ASCII are passed through only when they form well-formed UTF-8,
// and replaced with U+FFFD when they do not.
//
// This matters because refusal reasons interpolate strings taken straight from
// an uploaded file - a joint name, an extension name - and nothing obliges an
// untrusted GLB to hold valid UTF-8. Emitting those bytes raw produced a
// response body that was not valid UTF-8, so a client received a parse error in
// place of the sentence explaining why its upload was refused: the worse
// outcome, because it replaces a precise message with a mysterious one exactly
// when the creator needs the precise one. Valid text is unchanged, so this
// costs nothing for every message that was already fine.
std::string json_string(std::string_view value) {
    std::string result;
    result.reserve(value.size() + 2);
    result.push_back('"');
    for (std::size_t at = 0; at < value.size();) {
        const auto character = static_cast<unsigned char>(value[at]);
        switch (character) {
        case '"': result += "\\\""; ++at; continue;
        case '\\': result += "\\\\"; ++at; continue;
        case '\b': result += "\\b"; ++at; continue;
        case '\f': result += "\\f"; ++at; continue;
        case '\n': result += "\\n"; ++at; continue;
        case '\r': result += "\\r"; ++at; continue;
        case '\t': result += "\\t"; ++at; continue;
        default: break;
        }
        if (character < 0x20) {
            constexpr char hex[] = "0123456789abcdef";
            result += "\\u00";
            result.push_back(hex[character >> 4]);
            result.push_back(hex[character & 0x0f]);
            ++at;
        } else if (character < 0x80) {
            result.push_back(static_cast<char>(character));
            ++at;
        } else if (const auto length = utf8_sequence_length(value, at); length > 0) {
            result.append(value, at, length);
            at += length;
        } else {
            // One replacement per bad byte rather than per bad run, so the
            // damage in the output is proportional to the damage in the input.
            result += "\\ufffd";
            ++at;
        }
    }
    result.push_back('"');
    return result;
}

std::string to_json(const Status& value) {
    return "{\"status\":" + json_string(value.status) + '}';
}

std::string to_json(const Version& value) {
    return "{\"service\":" + json_string(value.service) +
           ",\"version\":" + json_string(value.version) +
           ",\"apiVersion\":" + json_string(value.api_version) + '}';
}

std::string to_json(const Error& value) {
    return "{\"code\":" + json_string(value.code) +
           ",\"message\":" + json_string(value.message) + '}';
}

} // namespace homeworldz::api
