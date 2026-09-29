#include "atomic_file.hpp"

#include <chrono>
#include <fstream>
#include <random>
#include <stdexcept>
#include <system_error>
#include <vector>

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

fs::path unique_path(const fs::path &first_choice) {
    if (!fs::exists(first_choice)) {
        return first_choice;
    }

    for (int n = 2;; ++n) {
        fs::path candidate = first_choice.parent_path() /
                             (first_choice.stem().string() + "." + std::to_string(n) +
                              first_choice.extension().string());

        if (!fs::exists(candidate)) {
            return candidate;
        }
    }
}

fs::path move_to_trash(const fs::path &source, const fs::path &trash_root,
                       const fs::path &relative_path) {
    fs::path destination = unique_path(trash_root / relative_path);
    fs::create_directories(destination.parent_path());

    std::error_code rename_ec;
    fs::rename(source, destination, rename_ec);

    if (rename_ec) {
        fs::copy_file(source, destination);
        fs::remove(source);
    }

    // Recorded as the trashing time, not the file's original content time,
    // so cleanup_trash can tell how long ago each file was deleted.
    std::error_code time_ec;
    fs::last_write_time(destination, fs::file_time_type::clock::now(), time_ec);

    return destination;
}

void cleanup_trash(const fs::path &trash_root, int retention_days) {
    if (retention_days <= 0 || !fs::exists(trash_root)) {
        return;
    }

    auto cutoff = fs::file_time_type::clock::now() - std::chrono::hours(24 * retention_days);

    for (const auto &entry : fs::recursive_directory_iterator(trash_root)) {
        if (!entry.is_regular_file()) {
            continue;
        }

        std::error_code time_ec;
        auto mtime = fs::last_write_time(entry.path(), time_ec);

        if (!time_ec && mtime < cutoff) {
            fs::remove(entry.path());
        }
    }

    std::vector<fs::path> directories;

    for (const auto &entry : fs::recursive_directory_iterator(trash_root)) {
        if (entry.is_directory()) {
            directories.push_back(entry.path());
        }
    }

    // Deepest paths first so a directory left empty by removing its only
    // (now-empty) subdirectory gets pruned too, in the same pass.
    std::sort(directories.begin(), directories.end(),
              [](const fs::path &a, const fs::path &b) { return a.string().size() > b.string().size(); });

    for (const auto &dir : directories) {
        std::error_code remove_ec;
        fs::remove(dir, remove_ec);
    }
}
