#include "security.hpp"

#include <cstdint>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    std::string input(reinterpret_cast<const char *>(data), size);

    is_safe_relative_path(input);
    is_secure_remote_url(input);

    return 0;
}
