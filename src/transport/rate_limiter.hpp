#pragma once

#include <chrono>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>

// Sliding-window rate limiter keyed by an arbitrary client key (e.g. an IP
// address). Idle client entries are swept out periodically so a stream of
// distinct keys (spoofed source IPs, churny clients) doesn't grow the
// tracking map without bound.
class RateLimiter {
  public:
    using Clock = std::chrono::steady_clock;

    explicit RateLimiter(int max_requests_per_minute,
                         std::function<Clock::time_point()> now_fn = Clock::now,
                         Clock::duration eviction_interval = std::chrono::minutes(1));

    bool allow(const std::string &client_key);

    // Number of client keys currently tracked; exposed for tests.
    std::size_t tracked_client_count();

  private:
    int max_requests_per_minute_;
    std::function<Clock::time_point()> now_fn_;
    Clock::duration eviction_interval_;
    Clock::time_point last_eviction_{};
    std::mutex mutex_;
    std::unordered_map<std::string, std::deque<Clock::time_point>> requests_by_client_;
};
