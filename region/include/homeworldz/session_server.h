#pragma once

// The region-session listener (docs/CLIENT2-TRANSPORT.md, option A): a
// WebSocket server on libwebsockets running its own service thread, speaking
// the session protocol of session_protocol.h. TLS is the fronting
// infrastructure's concern in this deployment (the grid's edge or a local
// proxy terminates wss); in-region TLS arrives with direct home-hosted
// serving.

#include "homeworldz/session_protocol.h"

#include <memory>
#include <string>
#include <string_view>

namespace homeworldz::session {

class Server {
public:
    struct Options {
        int port{};
        std::string region_name;
        // validator runs on the service thread; it is expected to block
        // briefly (one grid round trip) during auth only.
        TicketValidator validator;
    };

    // start returns a running server, or null when the listener could not be
    // created. The server stops and joins its thread on destruction.
    static std::unique_ptr<Server> start(Options options);
    ~Server();

    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    // broadcast_chat delivers one public chat line to every authenticated
    // session. Thread-safe; called from the simulation thread.
    void broadcast_chat(std::string_view from_name, std::string_view message);

    // session_count reports authenticated sessions, for logs and tests.
    int session_count() const;

private:
    Server();
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace homeworldz::session
