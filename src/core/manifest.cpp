#include "manifest.hpp"

#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>

using json = nlohmann::json;

Manifest load_manifest(const std::string& path) {
    std::ifstream file(path);

    if (!file) {
        throw std::runtime_error("Could not open manifest: " + path);
    }

    try {
        json root;
        file >> root;

        Manifest manifest;

        for (const auto& item : root.at("files")) {
            FileMeta meta;

            meta.path = item.at("path");
            meta.hash = item.at("hash");
            meta.size = item.at("size");
            meta.modified_time = item.at("modified_time");

            manifest[meta.path] = meta;
        }

        return manifest;
    } catch (const json::exception& error) {
        throw std::runtime_error("Malformed manifest " + path + ": " + error.what());
    }
}