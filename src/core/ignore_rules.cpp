#include "ignore_rules.hpp"

#include <fstream>
#include <string_view>

namespace fs = std::filesystem;

namespace {

bool glob_match(std::string_view pattern, std::string_view text) {
    if (pattern.empty())
        return text.empty();

    if (pattern.front() == '*') {
        if (glob_match(pattern.substr(1), text))
            return true;
        if (!text.empty())
            return glob_match(pattern, text.substr(1));
        return false;
    }

    if (text.empty())
        return false;

    if (pattern.front() == '?' || pattern.front() == text.front())
        return glob_match(pattern.substr(1), text.substr(1));

    return false;
}

std::string trim(const std::string &line) {
    size_t start = line.find_first_not_of(" \t\r\n");
    if (start == std::string::npos)
        return "";
    size_t end = line.find_last_not_of(" \t\r\n");
    return line.substr(start, end - start + 1);
}

} // namespace

void IgnoreRules::add_line(const std::string &raw_line) {
    std::string line = trim(raw_line);

    if (line.empty() || line.front() == '#' || line.front() == '!')
        return;

    IgnorePattern pattern;

    if (!line.empty() && line.back() == '/') {
        pattern.directory_only = true;
        line.pop_back();
    }
    if (line.empty())
        return;

    if (line.front() == '/') {
        pattern.anchored = true;
        line.erase(0, 1);
    } else if (line.find('/') != std::string::npos) {
        pattern.anchored = true;
    }

    if (line.empty())
        return;

    pattern.glob = line;
    patterns_.push_back(std::move(pattern));
}

bool IgnoreRules::matches(const fs::path &relative_path) const {
    if (patterns_.empty())
        return false;

    std::vector<std::string> components;
    for (const auto &part : relative_path)
        components.push_back(part.generic_string());

    if (components.empty())
        return false;

    for (const auto &pattern : patterns_) {
        size_t last_component = pattern.directory_only ? components.size() - 1 : components.size();

        if (pattern.anchored) {
            if (pattern.directory_only) {
                std::string joined;
                for (size_t i = 0; i < last_component; ++i) {
                    if (i > 0)
                        joined += '/';
                    joined += components[i];
                    if (glob_match(pattern.glob, joined))
                        return true;
                }
                continue;
            }

            std::string joined;
            for (size_t i = 0; i < last_component; ++i) {
                if (i > 0)
                    joined += '/';
                joined += components[i];
            }
            if (glob_match(pattern.glob, joined))
                return true;
            continue;
        }

        for (size_t i = 0; i < last_component; ++i) {
            if (glob_match(pattern.glob, components[i]))
                return true;
        }
    }

    return false;
}

IgnoreRules load_ignore_rules(const fs::path &vault_root) {
    IgnoreRules rules;

    fs::path ignore_file = vault_root / ".obsidian-sync-ignore";
    std::ifstream in(ignore_file);
    if (!in.is_open())
        return rules;

    std::string line;
    while (std::getline(in, line))
        rules.add_line(line);

    return rules;
}
