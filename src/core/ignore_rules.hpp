#pragma once

#include <filesystem>
#include <string>
#include <vector>

struct IgnorePattern {
    std::string glob;
    bool anchored = false;
    bool directory_only = false;
};

class IgnoreRules {
public:
    void add_line(const std::string &line);
    bool matches(const std::filesystem::path &relative_path) const;
    bool empty() const { return patterns_.empty(); }

private:
    std::vector<IgnorePattern> patterns_;
};

IgnoreRules load_ignore_rules(const std::filesystem::path &vault_root);
