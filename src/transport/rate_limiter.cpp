#include "rate_limiter.hpp"

RateLimiter::RateLimiter(int max_requests_per_minute,
                         std::function<Clock::time_point()> now_fn,
                         Clock::duration eviction_interval)
    : max_requests_per_minute_(max_requests_per_minute),
      now_fn_(std::move(now_fn)),
      eviction_interval_(eviction_interval) {}

static void trim_stale(std::deque<RateLimiter::Clock::time_point> &requests,
                       RateLimiter::Clock::time_point window_start) {
    while (!requests.empty() && requests.front() < window_start) {
        requests.pop_front();
    }
}

bool RateLimiter::allow(const std::string &client_key) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto now = now_fn_();
    auto window_start = now - std::chrono::minutes(1);

    // Evict idle clients before looking up/inserting this call's own entry,
    // so a freshly-created empty entry for client_key can never be the one
    // that gets erased out from under the reference below.
    if (now - last_eviction_ >= eviction_interval_) {
        last_eviction_ = now;

        for (auto it = requests_by_client_.begin(); it != requests_by_client_.end();) {
            trim_stale(it->second, window_start);

            if (it->second.empty()) {
                it = requests_by_client_.erase(it);
            } else {
                ++it;
            }
        }
    }

    auto &requests = requests_by_client_[client_key];
    trim_stale(requests, window_start);

    if (static_cast<int>(requests.size()) >= max_requests_per_minute_) {
        return false;
    }

    requests.push_back(now);
    return true;
}

std::size_t RateLimiter::tracked_client_count() {
    std::lock_guard<std::mutex> lock(mutex_);
    return requests_by_client_.size();
}
