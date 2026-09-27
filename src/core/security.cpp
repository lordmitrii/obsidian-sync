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

bool is_secure_remote_url(const std::string &url) {
    if (url.rfind("https://", 0) == 0) {
        return true;
    }

    const std::string http_prefix = "http://";

    if (url.rfind(http_prefix, 0) != 0) {
        return false;
    }

    std::string host_and_rest = url.substr(http_prefix.size());
    std::string host;

    if (!host_and_rest.empty() && host_and_rest.front() == '[') {
        // Bracketed IPv6 host, e.g. http://[::1]:8080/ — the closing ']'
        // marks the end of the host, since a bare ':' inside it is part of
        // the address rather than a port separator.
        auto closing_bracket = host_and_rest.find(']');
        host = host_and_rest.substr(0, closing_bracket == std::string::npos
                                            ? host_and_rest.size()
                                            : closing_bracket + 1);
    } else {
        host = host_and_rest.substr(0, host_and_rest.find_first_of(":/"));
    }

    return host == "127.0.0.1" || host == "localhost" || host == "[::1]";
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
