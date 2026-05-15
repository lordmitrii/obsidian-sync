#include "security.hpp"

#include <cstdlib>
#include <stdexcept>

std::string load_required_bearer_token() {
    const char *value = std::getenv("OBSIDIAN_SYNC_TOKEN");

    if (value == nullptr || std::string(value).empty()) {
        throw std::runtime_error("OBSIDIAN_SYNC_TOKEN is required for HTTP modes");
    }

    return value;
}

std::string bearer_authorization_header(const std::string &token) {
    return "Bearer " + token;
}
