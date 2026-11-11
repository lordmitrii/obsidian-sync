#include "time_utils.hpp"

#include <ctime>

std::string format_timestamp_iso8601(std::chrono::system_clock::time_point time) {
    std::time_t time_t_value = std::chrono::system_clock::to_time_t(time);
    std::tm utc_tm{};
#if defined(_WIN32)
    gmtime_s(&utc_tm, &time_t_value);
#else
    gmtime_r(&time_t_value, &utc_tm);
#endif
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &utc_tm);
    return buffer;
}

std::string current_timestamp_iso8601() {
    return format_timestamp_iso8601(std::chrono::system_clock::now());
}
