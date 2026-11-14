#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_set>

class ObjectStore {
  public:
    explicit ObjectStore(std::filesystem::path objects_dir);

    std::string put(const std::string &content) const;

    std::optional<std::string> get(const std::string &hash) const;

    bool has(const std::string &hash) const;

    void prune(const std::unordered_set<std::string> &keep_hashes) const;

  private:
    std::filesystem::path objects_dir_;
};
