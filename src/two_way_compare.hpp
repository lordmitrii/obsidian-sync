#pragma once

#include "manifest.hpp"
#include "manifest_action.hpp"

#include <vector>

std::vector<ManifestAction> compare_two_way(
    const Manifest& local,
    const Manifest& remote
);
