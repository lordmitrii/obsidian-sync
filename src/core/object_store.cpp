#include "object_store.hpp"

#include "atomic_file.hpp"
#include "hasher.hpp"

#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

ObjectStore::ObjectStore(fs::path objects_dir) : objects_dir_(std::move(objects_dir)) {}

std::string ObjectStore::put(const std::string &content) const {
    std::string hash = sha256_bytes(content);

    if (!has(hash)) {
        fs::create_directories(objects_dir_);
        atomic_write_file(objects_dir_ / hash, content);
    }

    return hash;
}

std::optional<std::string> ObjectStore::get(const std::string &hash) const {
    fs::path path = objects_dir_ / hash;

    if (!fs::exists(path)) {
        return std::nullopt;
    }

    std::ifstream file(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

bool ObjectStore::has(const std::string &hash) const { return fs::exists(objects_dir_ / hash); }

void ObjectStore::prune(const std::unordered_set<std::string> &keep_hashes) const {
    if (!fs::exists(objects_dir_)) {
        return;
    }

    for (const auto &entry : fs::directory_iterator(objects_dir_)) {
        if (!entry.is_regular_file()) {
            continue;
        }

        if (keep_hashes.count(entry.path().filename().string()) == 0) {
            fs::remove(entry.path());
        }
    }
}
