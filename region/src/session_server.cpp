#include "homeworldz/session_server.h"

#include <libwebsockets.h>

#include <atomic>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>
#include <unordered_set>
#include <vector>

namespace homeworldz::session {
namespace {

// One connection's state, owned by the service thread.
struct Connection {
    SessionCore core;
    std::string inbound;
    std::deque<std::string> outbox;

    explicit Connection(SessionCore state) : core(std::move(state)) {}
};

// authTimeoutSeconds bounds how long an unauthenticated connection may hold
// a socket, matching the grid channel's rule.
constexpr int auth_timeout_seconds = 10;

// inboundLimit bounds one message; session traffic is small.
constexpr std::size_t inbound_limit = 64 * 1024;

} // namespace

struct Server::State {
    Options options;
    lws_context* context{};
    std::thread service;
    std::atomic<bool> running{true};
    std::atomic<int> established{0};

    // Chat queued by the simulation thread, drained on the service thread.
    std::mutex pending_mutex;
    std::vector<std::string> pending_broadcasts;

    // Live connections; service-thread only.
    std::unordered_set<lws*> connections;

    static int callback(lws* wsi, lws_callback_reasons reason, void* user, void* in, size_t len);
    void service_loop();
    void drain_broadcasts();
    bool flush_one(lws* wsi, Connection* connection);

    static State* of(lws* wsi) {
        return static_cast<State*>(lws_context_user(lws_get_context(wsi)));
    }

    static void queue_messages(lws* wsi, Connection* connection, std::vector<std::string> messages,
                               bool close_after, const std::string& reason) {
        for (auto& message : messages) connection->outbox.push_back(std::move(message));
        if (close_after) {
            lws_close_reason(wsi, LWS_CLOSE_STATUS_POLICY_VIOLATION,
                             reinterpret_cast<unsigned char*>(const_cast<char*>(reason.data())),
                             reason.size());
        }
        lws_callback_on_writable(wsi);
    }
};

int Server::State::callback(lws* wsi, lws_callback_reasons reason, void* user, void* in, size_t len) {
    auto* state = of(wsi);
    auto** slot = static_cast<Connection**>(user);
    switch (reason) {
    case LWS_CALLBACK_ESTABLISHED: {
        *slot = new Connection(SessionCore(state->options.region_name, state->options.validator));
        state->connections.insert(wsi);
        lws_set_timeout(wsi, PENDING_TIMEOUT_USER_REASON_BASE, auth_timeout_seconds);
        return 0;
    }
    case LWS_CALLBACK_CLOSED: {
        if (slot && *slot) {
            if ((*slot)->core.established()) state->established.fetch_sub(1);
            delete *slot;
            *slot = nullptr;
        }
        state->connections.erase(wsi);
        return 0;
    }
    case LWS_CALLBACK_RECEIVE: {
        auto* connection = slot ? *slot : nullptr;
        if (!connection) return -1;
        if (connection->inbound.size() + len > inbound_limit) return -1;
        connection->inbound.append(static_cast<const char*>(in), len);
        if (!lws_is_final_fragment(wsi)) return 0;
        std::string message;
        message.swap(connection->inbound);

        const auto was_established = connection->core.established();
        const auto result = !lws_frame_is_binary(wsi)
                                ? connection->core.handle_text(message)
                                : connection->core.handle_binary();
        if (!was_established && connection->core.established()) {
            state->established.fetch_add(1);
            // Authenticated: the auth deadline no longer applies.
            lws_set_timeout(wsi, NO_PENDING_TIMEOUT, 0);
        }
        queue_messages(wsi, connection, result.send, result.close, result.close_reason);
        if (result.close && result.send.empty()) return -1;
        return 0;
    }
    case LWS_CALLBACK_SERVER_WRITEABLE: {
        auto* connection = slot ? *slot : nullptr;
        if (!connection) return -1;
        if (!state->flush_one(wsi, connection)) return -1;
        if (!connection->outbox.empty()) lws_callback_on_writable(wsi);
        return 0;
    }
    case LWS_CALLBACK_EVENT_WAIT_CANCELLED:
        state->drain_broadcasts();
        return 0;
    default:
        return 0;
    }
}

bool Server::State::flush_one(lws* wsi, Connection* connection) {
    if (connection->outbox.empty()) return true;
    auto& message = connection->outbox.front();
    std::vector<unsigned char> frame(LWS_PRE + message.size());
    std::memcpy(frame.data() + LWS_PRE, message.data(), message.size());
    const auto written = lws_write(wsi, frame.data() + LWS_PRE, message.size(), LWS_WRITE_TEXT);
    if (written < 0 || static_cast<std::size_t>(written) != message.size()) return false;
    connection->outbox.pop_front();
    return true;
}

void Server::State::drain_broadcasts() {
    std::vector<std::string> drained;
    {
        std::lock_guard<std::mutex> hold(pending_mutex);
        drained.swap(pending_broadcasts);
    }
    if (drained.empty()) return;
    for (auto* wsi : connections) {
        void* user = lws_wsi_user(wsi);
        auto* connection = user ? *static_cast<Connection**>(user) : nullptr;
        if (!connection || !connection->core.established()) continue;
        for (const auto& message : drained) connection->outbox.push_back(message);
        lws_callback_on_writable(wsi);
    }
}

void Server::State::service_loop() {
    while (running.load()) {
        if (lws_service(context, 0) < 0) break;
    }
}

Server::Server() = default;

std::unique_ptr<Server> Server::start(Options options) {
    if (options.port <= 0 || options.port > 65535) return nullptr;

    static const lws_protocols protocols[] = {
        {"homeworldz-session", &State::callback, sizeof(Connection*), inbound_limit, 0, nullptr, 0},
        LWS_PROTOCOL_LIST_TERM,
    };

    auto server = std::unique_ptr<Server>(new Server());
    server->state_ = std::make_unique<State>();
    server->state_->options = std::move(options);

    lws_context_creation_info info{};
    info.port = server->state_->options.port;
    info.protocols = protocols;
    info.user = server->state_.get();
    info.gid = static_cast<gid_t>(-1);
    info.uid = static_cast<uid_t>(-1);
    info.options = LWS_SERVER_OPTION_HTTP_HEADERS_SECURITY_BEST_PRACTICES_ENFORCE;

    lws_set_log_level(LLL_ERR | LLL_WARN, nullptr);
    server->state_->context = lws_create_context(&info);
    if (!server->state_->context) return nullptr;
    server->state_->service = std::thread([state = server->state_.get()] { state->service_loop(); });
    return server;
}

Server::~Server() {
    if (!state_) return;
    state_->running.store(false);
    if (state_->context) lws_cancel_service(state_->context);
    if (state_->service.joinable()) state_->service.join();
    if (state_->context) lws_context_destroy(state_->context);
}

void Server::broadcast_chat(std::string_view from_name, std::string_view message) {
    if (!state_) return;
    const auto rendered = encode_envelope("chat", {},
        "{\"from\":" + json_string(from_name) + ",\"message\":" + json_string(message) + "}");
    {
        std::lock_guard<std::mutex> hold(state_->pending_mutex);
        state_->pending_broadcasts.push_back(rendered);
    }
    lws_cancel_service(state_->context);
}

int Server::session_count() const {
    return state_ ? state_->established.load() : 0;
}

} // namespace homeworldz::session
