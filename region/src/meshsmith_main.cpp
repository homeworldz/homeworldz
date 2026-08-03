// homeworldz-meshsmith: the grid-side conversion worker of ADR 0033. Claims
// sl-mesh rendition jobs from the grid's queue, reads the canonical GLB out
// of the vault, derives the type-49 payload, and stores it back — over HTTP,
// on the worker credential, so it can run anywhere the grid trusts and
// regions never gain the power to write renditions (ADR 0028).
//
// Deliberately boring: one job at a time, failures reported with the
// converter's own reason, an empty queue answered by sleeping. Dying is safe
// — the lease lapses and the job is claimable again.
#include "homeworldz/grid_client.h"
#include "homeworldz/image.h"
#include "homeworldz/mesh_convert.h"

#include <chrono>
#include <csignal>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>

#ifdef _WIN32
#include <winsock2.h>
#endif

namespace {

volatile std::sig_atomic_t stopping = 0;
void handle_stop(int) { stopping = 1; }

// The grid's job JSON is a flat object this worker itself defined the shape
// of; a targeted field read keeps the binary free of a JSON dependency.
std::string field(const std::string& body, std::string_view name) {
    const auto marker = "\"" + std::string(name) + "\":\"";
    const auto start = body.find(marker);
    if (start == std::string::npos) return {};
    const auto begin = start + marker.size();
    const auto end = body.find('"', begin);
    if (end == std::string::npos) return {};
    return body.substr(begin, end - begin);
}

std::string json_string(std::string_view value) {
    std::string out = "\"";
    for (const auto character : value) {
        if (character == '"' || character == '\\') out.push_back('\\');
        if (static_cast<unsigned char>(character) < 0x20) continue;
        out.push_back(character);
    }
    out.push_back('"');
    return out;
}

void log(std::string_view level, std::string_view message, std::string extra = {}) {
    std::cout << "{\"level\":\"" << level << "\",\"message\":" << json_string(message)
              << extra << "}" << std::endl;
}

} // namespace

int main(int argc, char** argv) {
    std::string grid_url;
    std::string worker_token;
    int interval_seconds = 5;
    bool once = false;
    bool regenerate = false;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument = argv[index];
        if (argument == "--grid" && index + 1 < argc) grid_url = argv[++index];
        else if (argument == "--token" && index + 1 < argc) worker_token = argv[++index];
        else if (argument == "--interval" && index + 1 < argc)
            interval_seconds = std::atoi(argv[++index]);
        else if (argument == "--once") once = true;
        else if (argument == "--regenerate") regenerate = true;
    }
    if (grid_url.empty() || worker_token.empty()) {
        std::cerr << "usage: homeworldz-meshsmith --grid <url> --token <worker token>"
                     " [--interval seconds] [--once] [--regenerate]" << std::endl;
        return 2;
    }
    if (interval_seconds < 1) interval_seconds = 1;
#ifdef _WIN32
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) return 1;
#endif
    std::signal(SIGINT, handle_stop);
    std::signal(SIGTERM, handle_stop);
    log("info", "meshsmith started",
        ",\"generator\":" + json_string(homeworldz::mesh::generator) +
        ",\"grid\":" + json_string(grid_url));

    const auto transport = homeworldz::grid::socket_transport(grid_url, worker_token);
    if (regenerate) {
        // The upgrade sweep: everything a different generator produced
        // returns to the queue, and the loop below reconverts it.
        try {
            for (const auto* swept_kind : {"sl-mesh", "gltf", "j2c-texture", "png-texture"}) {
                const auto swept = transport->send("POST", "/api/v1/rendition-jobs/regenerate",
                    std::string(R"({"kind":")") + swept_kind + R"(","generator":)" +
                        json_string(homeworldz::mesh::generator) + "}");
                if (swept.status_code == 200)
                    log("info", "regeneration sweep", ",\"kind\":" + json_string(swept_kind) +
                        ",\"response\":" + json_string(swept.body));
                else
                    log("warning", "regeneration sweep refused",
                        ",\"kind\":" + json_string(swept_kind) +
                        ",\"status\":" + std::to_string(swept.status_code));
            }
        } catch (const std::exception& error) {
            log("warning", "regeneration sweep failed", ",\"error\":" + json_string(error.what()));
        }
    }
    while (stopping == 0) {
        homeworldz::grid::HttpResponse claim;
        try {
            claim = transport->send("POST", "/api/v1/rendition-jobs/claim",
                R"({"kinds":["sl-mesh","gltf","j2c-texture","png-texture"],"leaseSeconds":300})");
        } catch (const std::exception& error) {
            log("warning", "claim failed", ",\"error\":" + json_string(error.what()));
            claim.status_code = 0;
        }
        if (claim.status_code != 200) {
            // 204 is the queue's normal resting state; anything else waits too.
            if (once) break;
            std::this_thread::sleep_for(std::chrono::seconds(interval_seconds));
            continue;
        }
        const auto job_id = field(claim.body, "id");
        const auto asset_id = field(claim.body, "assetId");
        const auto kind = field(claim.body, "kind");
        if (job_id.empty() || asset_id.empty() || kind.empty()) {
            log("error", "claimed job has no identity", ",\"body\":" + json_string(claim.body));
            continue;
        }
        const auto give_up = [&](const std::string& reason) {
            log("warning", "conversion failed",
                ",\"assetId\":" + json_string(asset_id) + ",\"error\":" + json_string(reason));
            try {
                transport->send("POST", "/api/v1/rendition-jobs/" + job_id + "/fail",
                                "{\"error\":" + json_string(reason) + "}");
            } catch (const std::exception& error) {
                log("error", "failure report failed", ",\"error\":" + json_string(error.what()));
            }
        };
        try {
            const auto canonical = transport->send("GET", "/api/v1/vault/assets/" + asset_id, {});
            if (canonical.status_code != 200 || canonical.body.empty()) {
                give_up("canonical bytes unavailable (vault answered " +
                        std::to_string(canonical.status_code) + ")");
                continue;
            }
            const auto content = std::span(
                reinterpret_cast<const std::byte*>(canonical.body.data()),
                canonical.body.size());
            // Which direction this job runs is the canonical format's business,
            // not the job's: a GLB derives the viewer's type-49, a stored
            // type-49 derives the modern client's glTF (ADR 0033 M1 and M2).
            std::vector<std::byte> derived;
            std::string detail;
            if (kind == "sl-mesh") {
                const auto conversion = homeworldz::mesh::convert_glb(content);
                if (!conversion.ok) {
                    give_up(conversion.error);
                    continue;
                }
                derived = conversion.sl_mesh;
                detail = ",\"faces\":" + std::to_string(conversion.faces) +
                    ",\"highTriangles\":" + std::to_string(conversion.high_triangles) +
                    ",\"lowestTriangles\":" + std::to_string(conversion.lowest_triangles);
            } else if (kind == "gltf") {
                const auto conversion = homeworldz::mesh::gltf_from_sl_mesh(content);
                if (!conversion.ok) {
                    give_up(conversion.error);
                    continue;
                }
                derived = conversion.glb;
                detail = ",\"primitives\":" + std::to_string(conversion.primitives) +
                    ",\"vertices\":" + std::to_string(conversion.vertices) +
                    ",\"triangles\":" + std::to_string(conversion.triangles);
            } else if (kind == "j2c-texture") {
                // A viewer cannot read the PNG or JPEG a GLB embeds, so the
                // canonical image derives the JPEG2000 the legacy texture
                // pipeline fetches - the same direction as sl-mesh, for
                // images (ADR 0033 M3).
                const std::vector<std::uint8_t> source(
                    reinterpret_cast<const std::uint8_t*>(canonical.body.data()),
                    reinterpret_cast<const std::uint8_t*>(canonical.body.data()) +
                        canonical.body.size());
                const auto decoded = homeworldz::image::decode_png_or_jpeg(source);
                if (!decoded) {
                    give_up("the canonical image is neither PNG nor JPEG");
                    continue;
                }
                const auto encoded = homeworldz::image::encode_j2c(*decoded);
                if (!encoded) {
                    give_up("JPEG2000 encoding failed");
                    continue;
                }
                derived.assign(reinterpret_cast<const std::byte*>(encoded->data()),
                               reinterpret_cast<const std::byte*>(encoded->data()) +
                                   encoded->size());
                detail = ",\"width\":" + std::to_string(decoded->width) +
                    ",\"height\":" + std::to_string(decoded->height) +
                    ",\"channels\":" + std::to_string(decoded->channels);
            } else if (kind == "png-texture") {
                // The reverse of j2c-texture, and the reason it exists: a
                // texture a viewer uploaded is canonically JPEG2000, which the
                // first-party client refuses by rule, so every texture created
                // in Firestorm was invisible to it. Same asymmetry the gltf
                // rendition fixed for mesh.
                const std::vector<std::uint8_t> source(
                    reinterpret_cast<const std::uint8_t*>(canonical.body.data()),
                    reinterpret_cast<const std::uint8_t*>(canonical.body.data()) +
                        canonical.body.size());
                const auto decoded = homeworldz::image::decode_j2c(source);
                if (!decoded) {
                    give_up("the canonical image is not JPEG2000");
                    continue;
                }
                const auto encoded = homeworldz::image::encode_png(*decoded);
                if (!encoded) {
                    give_up("PNG encoding failed");
                    continue;
                }
                derived.assign(reinterpret_cast<const std::byte*>(encoded->data()),
                               reinterpret_cast<const std::byte*>(encoded->data()) +
                                   encoded->size());
                detail = ",\"width\":" + std::to_string(decoded->width) +
                    ",\"height\":" + std::to_string(decoded->height) +
                    ",\"channels\":" + std::to_string(decoded->channels);
            } else {
                give_up("this worker does not convert " + kind);
                continue;
            }
            const auto put = transport->send("PUT",
                "/api/v1/assets/" + asset_id + "/renditions/" + kind + "?generator=" +
                    std::string(homeworldz::mesh::generator),
                std::string_view(reinterpret_cast<const char*>(derived.data()),
                                 derived.size()));
            if (put.status_code != 200) {
                give_up("rendition store answered " + std::to_string(put.status_code));
                continue;
            }
            log("info", "converted",
                ",\"assetId\":" + json_string(asset_id) +
                ",\"kind\":" + json_string(kind) + detail +
                ",\"bytes\":" + std::to_string(derived.size()));
        } catch (const std::exception& error) {
            give_up(std::string("worker error: ") + error.what());
        }
        if (once) break;
    }
    log("info", "meshsmith stopping");
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
