#pragma once

#include <string>
#include <string_view>

namespace homeworldz::http {

std::string response_for(std::string_view request);

} // namespace homeworldz::http

