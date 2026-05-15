#pragma once

#include <cstdint>
#include <string>

struct FileMeta {
    std::string path;
    std::string hash;
    std::uintmax_t size;
    std::int64_t modified_time;
};