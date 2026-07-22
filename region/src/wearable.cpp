#include "homeworldz/wearable.h"

#include <istream>
#include <sstream>
#include <string>

namespace homeworldz::viewer {
namespace {

std::string trim(const std::string& s) {
    const char* ws = " \t\r\n";
    const auto begin = s.find_first_not_of(ws);
    if (begin == std::string::npos) return {};
    const auto end = s.find_last_not_of(ws);
    return s.substr(begin, end - begin + 1);
}

// Consume a `{ ... }` block that follows a permissions/sale_info keyword,
// balancing braces (which sit on their own lines in the LLWearable format).
void skip_brace_block(std::istream& in) {
    std::string line;
    int depth = 0;
    bool opened = false;
    while (std::getline(in, line)) {
        for (char ch : line) {
            if (ch == '{') {
                ++depth;
                opened = true;
            } else if (ch == '}') {
                --depth;
            }
        }
        if (opened && depth <= 0) return;
    }
}

}  // namespace

std::optional<Wearable> parse_wearable(std::string_view text) {
    std::istringstream in{std::string(text)};
    std::string line;
    Wearable w;

    // Header: "LLWearable version <N>".
    if (!std::getline(in, line)) return std::nullopt;
    {
        std::istringstream hs(line);
        std::string tag, ver;
        int v = 0;
        hs >> tag >> ver >> v;
        if (tag != "LLWearable" || ver != "version") return std::nullopt;
        w.version = v;
    }

    // Name is the whole next line.
    if (!std::getline(in, line)) return std::nullopt;
    w.name = trim(line);

    bool have_type = false;
    while (std::getline(in, line)) {
        const std::string t = trim(line);
        if (t.empty()) continue;

        std::istringstream ls(t);
        std::string key;
        ls >> key;

        if (key == "permissions" || key == "sale_info") {
            skip_brace_block(in);
        } else if (key == "type") {
            int ty = -1;
            ls >> ty;
            w.type = static_cast<WearableType>(ty);
            have_type = true;
        } else if (key == "parameters") {
            int count = 0;
            ls >> count;
            for (int i = 0; i < count && std::getline(in, line); ++i) {
                std::istringstream ps(trim(line));
                std::uint32_t id = 0;
                double value = 0;
                if (ps >> id >> value) w.parameters[id] = value;
            }
        } else if (key == "textures") {
            int count = 0;
            ls >> count;
            for (int i = 0; i < count && std::getline(in, line); ++i) {
                std::istringstream ts(trim(line));
                std::uint32_t index = 0;
                std::string uuid_text;
                if (ts >> index >> uuid_text) {
                    if (auto id = parse_uuid(uuid_text)) w.textures[index] = *id;
                }
            }
        }
        // Unknown top-level tokens are ignored for forward compatibility.
    }

    if (!have_type) return std::nullopt;
    return w;
}

}  // namespace homeworldz::viewer
