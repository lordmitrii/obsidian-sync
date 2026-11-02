#include "manifest.hpp"

#include <cstdint>
#include <exception>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    std::string json_text(reinterpret_cast<const char *>(data), size);

    try {
        parse_manifest_json(json_text);
    } catch (const std::exception &) {
    }

    return 0;
}
