#include "manifest.hpp"

#include "security.hpp"

#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <stdexcept>

using json = nlohmann::json;

Manifest parse_manifest_json(const std::string& json_text) {
    json root = json::parse(json_text);
    Manifest manifest;

    for (const auto& item : root.at("files")) {
        FileMeta meta;

        meta.path = item.at("path");
        meta.hash = item.at("hash");
        meta.size = item.at("size");
        meta.modified_time = item.at("modified_time");

        if (!is_safe_relative_path(meta.path)) {
            throw std::runtime_error("Unsafe path in manifest: " + meta.path);
        }

        manifest[meta.path] = meta;
    }

    return manifest;
}

Manifest load_manifest(const std::string& path) {
    std::ifstream file(path);

    if (!file) {
        throw std::runtime_error("Could not open manifest: " + path);
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();

    try {
        return parse_manifest_json(buffer.str());
    } catch (const std::exception& error) {
        throw std::runtime_error("Malformed manifest " + path + ": " + error.what());
    }
}