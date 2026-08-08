// What the upload gate says about a GLB, without uploading it.
//
// The acceptance policy is published so creators can check their own content
// (ADR 0033), but a published document still leaves them running the file
// against a real region to find out. This answers the same question offline,
// with the identical code path — not a reimplementation of the rules, which
// would be a second copy able to disagree with the first.
#include "homeworldz/mesh_acceptance.h"

#include <cstddef>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: homeworldz-mesh-diag <file.glb> [more.glb ...]\n";
        return 2;
    }
    int refused = 0;
    for (int index = 1; index < argc; ++index) {
        std::ifstream input(argv[index], std::ios::binary);
        if (!input) {
            std::cout << argv[index] << ": cannot be opened\n";
            refused = 1;
            continue;
        }
        const std::vector<char> raw{std::istreambuf_iterator<char>(input),
                                    std::istreambuf_iterator<char>()};
        std::vector<std::byte> bytes(raw.size());
        for (std::size_t at = 0; at < raw.size(); ++at)
            bytes[at] = static_cast<std::byte>(raw[at]);
        const auto result = homeworldz::mesh::validate_glb(bytes);
        std::cout << argv[index] << ": " << (result.accepted ? "accepted" : "REFUSED");
        if (!result.accepted) {
            std::cout << " - " << result.reason;
            refused = 1;
        } else {
            std::cout << " (" << result.triangles << " triangles, " << result.materials
                      << " materials, " << result.textures << " textures)";
        }
        std::cout << '\n';
    }
    return refused;
}
