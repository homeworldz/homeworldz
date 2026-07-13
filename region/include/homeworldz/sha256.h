#pragma once

#include <cstddef>
#include <span>
#include <string>

namespace homeworldz::crypto {

std::string sha256_hex(std::span<const std::byte> data);

} // namespace homeworldz::crypto
