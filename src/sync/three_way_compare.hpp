#pragma once

#include "manifest.hpp"
#include "manifest_action.hpp"

#include <vector>

std::vector<ManifestAction> compare_three_way(
    const Manifest& base,
    const Manifest& local,
    const Manifest& remote
);
