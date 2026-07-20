#include "homeworldz/falcon_runtime.h"

#include <cassert>
#include <string>
#include <vector>

int main() {
    using namespace std::string_literals;
    using homeworldz::script::FalconHostMessage;
    using homeworldz::script::FalconRuntime;
    using homeworldz::script::Identity;

    std::vector<FalconHostMessage> messages;
    FalconRuntime runtime([&](FalconHostMessage message) {
        messages.push_back(std::move(message));
    });
    const Identity identity{
        "asset", "item", "object", "owner"};
    const auto loaded = runtime.rez(identity, R"LSL(
        default { state_entry() { llSay(7, "Hello, Avatar!"); } }
    )LSL", true);
    assert(loaded.compiled && loaded.running && runtime.size() == 1);
    const auto tick = runtime.run_tick(1000, 100);
    assert(tick.scripts_visited == 1 && tick.instructions != 0 && tick.trapped == 0);
    assert(messages.size() == 1 && !messages[0].owner_only && messages[0].channel == 7 &&
           messages[0].text == "Hello, Avatar!" &&
           messages[0].identity.inventory_item_id == "item");

    messages.clear();
    const std::string firestorm_source =
        "default { state_entry() { llOwnerSay(\"saved\"); } }\0"s;
    const auto firestorm_upload = runtime.rez(
        {"firestorm-asset", "firestorm-item", "firestorm-object", "owner"},
        firestorm_source, true);
    assert(firestorm_upload.compiled && firestorm_upload.running);
    runtime.run_tick();
    assert(messages.size() == 1 && messages[0].owner_only &&
           messages[0].text == "saved");

    messages.clear();
    const auto stopped = runtime.rez(
        {"asset2", "item2", "object", "owner"},
        "default { state_entry() { llOwnerSay(\"not yet\"); } }", false);
    assert(stopped.compiled && !stopped.running && runtime.size() == 3);
    runtime.run_tick();
    assert(messages.empty());
    assert(runtime.set_enabled("object", "item2", true));
    runtime.run_tick();
    assert(messages.empty()); // enabling does not synthesize state_entry after an inactive rez

    const auto invalid = runtime.rez(
        {"bad", "bad", "object", "owner"},
        "default { state_entry() { integer value = \"bad\"; } }", true);
    assert(!invalid.compiled && !invalid.diagnostic.empty() && runtime.size() == 3);

    assert(runtime.erase("object", "item") && runtime.size() == 2);
    return 0;
}
