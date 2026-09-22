#include "atomic_file.hpp"

#include <fstream>
#include <random>
#include <stdexcept>
#include <system_error>

namespace fs = std::filesystem;

static fs::path temp_path_for(const fs::path &destination) {
    static thread_local std::mt19937_64 rng{std::random_device{}()};
    return destination.parent_path() /
           (destination.filename().string() + ".tmp-" + std::to_string(rng()));
}

void atomic_write_file(const fs::path &path, const std::string &body) {
    fs::create_directories(path.parent_path());
    fs::path temp_path = temp_path_for(path);

    {
        std::ofstream out(temp_path, std::ios::binary);

        if (!out) {
            throw std::runtime_error("Failed to open temp file for writing: " + temp_path.string());
        }

        out.write(body.data(), static_cast<std::streamsize>(body.size()));

        if (!out.good()) {
            out.close();
            fs::remove(temp_path);
            throw std::runtime_error("Failed to write temp file: " + temp_path.string());
        }
    }

    std::error_code ec;
    fs::rename(temp_path, path, ec);

    if (ec) {
        fs::remove(temp_path);
        throw std::runtime_error("Failed to move temp file into place: " + path.string() + ": " +
                                 ec.message());
    }
}

void atomic_copy_file(const fs::path &source, const fs::path &destination) {
    fs::create_directories(destination.parent_path());
    fs::path temp_path = temp_path_for(destination);

    std::error_code copy_ec;
    fs::copy_file(source, temp_path, fs::copy_options::overwrite_existing, copy_ec);

    if (copy_ec) {
        fs::remove(temp_path);
        throw std::runtime_error("Failed to copy file: " + source.string() + ": " +
                                 copy_ec.message());
    }

    std::error_code rename_ec;
    fs::rename(temp_path, destination, rename_ec);

    if (rename_ec) {
        fs::remove(temp_path);
        throw std::runtime_error("Failed to move copied file into place: " + destination.string() +
                                 ": " + rename_ec.message());
    }
}
