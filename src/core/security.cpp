#include "security.hpp"

#include <cstddef>
#include <cstdlib>
#include <filesystem>
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

bool constant_time_equals(const std::string &a, const std::string &b) {
    if (a.size() != b.size()) {
        return false;
    }

    unsigned char diff = 0;

    for (std::size_t i = 0; i < a.size(); ++i) {
        diff |= static_cast<unsigned char>(a[i]) ^ static_cast<unsigned char>(b[i]);
    }

    return diff == 0;
}

bool is_safe_relative_path(const std::string &path) {
    if (path.empty()) {
        return false;
    }

    std::filesystem::path parsed(path);

    if (parsed.is_absolute()) {
        return false;
    }

    for (const auto &part : parsed) {
        if (part == "..") {
            return false;
        }
    }

    return true;
}
