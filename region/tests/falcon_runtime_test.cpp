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

    // touch_start dispatch: an enabled script in the touched object receives the
    // event and fires it on the next tick.
    messages.clear();
    const auto touch_rez = runtime.rez(
        {"touch-asset", "touch-item", "touch-object", "owner"},
        "default { touch_start(integer n) { llOwnerSay(\"Touched!\"); } }", true);
    assert(touch_rez.compiled && touch_rez.running);
    runtime.run_tick(); // drains the ctor-dispatched state_entry (there is none here)
    assert(messages.empty());
    // A non-matching object id reaches no script; the touched object reaches one.
    assert(runtime.dispatch_touch_start("no-such-object", 1) == 0);
    assert(runtime.dispatch_touch_start("touch-object", 1) == 1);
    runtime.run_tick();
    assert(messages.size() == 1 && messages[0].owner_only &&
           messages[0].text == "Touched!" &&
           messages[0].identity.inventory_item_id == "touch-item");

    // Queued touches drain one per idle tick rather than clobbering each other.
    messages.clear();
    assert(runtime.dispatch_touch_start("touch-object", 1) == 1);
    assert(runtime.dispatch_touch_start("touch-object", 1) == 1);
    runtime.run_tick();
    assert(messages.size() == 1);
    runtime.run_tick();
    assert(messages.size() == 2);

    // A disabled script accepts no touch, and a script without a touch_start
    // handler is not counted as a recipient.
    messages.clear();
    const auto silent = runtime.rez(
        {"silent-asset", "silent-item", "silent-object", "owner"},
        "default { state_entry() { llOwnerSay(\"ready\"); } }", true);
    assert(silent.compiled && silent.running);
    runtime.run_tick();
    messages.clear();
    assert(runtime.dispatch_touch_start("silent-object", 1) == 0);
    assert(runtime.set_enabled("touch-object", "touch-item", false));
    assert(runtime.dispatch_touch_start("touch-object", 1) == 0);
    runtime.run_tick();
    assert(messages.empty());
    return 0;
}
