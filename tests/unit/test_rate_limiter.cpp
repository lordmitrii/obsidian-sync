#include "rate_limiter.hpp"

#include <chrono>
#include <iostream>
#include <string>

static int failures = 0;

static void expect(bool condition, const std::string &case_name) {
    if (!condition) {
        std::cerr << "FAIL [" << case_name << "]\n";
        ++failures;
    }
}

// A clock the test advances by hand, so window sliding and eviction can be
// tested without sleeping.
struct FakeClock {
    RateLimiter::Clock::time_point now{};

    RateLimiter::Clock::time_point operator()() const { return now; }

    void advance(std::chrono::steady_clock::duration d) { now += d; }
};

static void test_allows_up_to_the_limit_then_blocks() {
    FakeClock clock;
    RateLimiter limiter(3, [&clock] { return clock(); });

    expect(limiter.allow("a"), "first_request_allowed");
    expect(limiter.allow("a"), "second_request_allowed");
    expect(limiter.allow("a"), "third_request_allowed");
    expect(!limiter.allow("a"), "fourth_request_blocked");
}

static void test_distinct_clients_have_separate_buckets() {
    FakeClock clock;
    RateLimiter limiter(1, [&clock] { return clock(); });

    expect(limiter.allow("a"), "client_a_first_request_allowed");
    expect(!limiter.allow("a"), "client_a_second_request_blocked");
    expect(limiter.allow("b"), "client_b_unaffected_by_a's_limit");
}

static void test_old_requests_fall_out_of_the_window() {
    FakeClock clock;
    RateLimiter limiter(1, [&clock] { return clock(); });

    expect(limiter.allow("a"), "first_request_allowed");
    expect(!limiter.allow("a"), "immediate_retry_blocked");

    clock.advance(std::chrono::minutes(1) + std::chrono::seconds(1));

    expect(limiter.allow("a"), "request_allowed_after_window_slides_past");
}

static void test_idle_clients_are_evicted() {
    FakeClock clock;
    RateLimiter limiter(5, [&clock] { return clock(); }, std::chrono::minutes(1));

    limiter.allow("a");
    limiter.allow("b");
    limiter.allow("c");
    expect(limiter.tracked_client_count() == 3, "three_distinct_clients_tracked");

    // Push every existing bucket's single timestamp out of the window, then
    // make one more call (for an unrelated client) past the eviction
    // interval so the sweep runs.
    clock.advance(std::chrono::minutes(2));
    limiter.allow("d");

    expect(limiter.tracked_client_count() == 1, "idle_clients_evicted_only_active_one_remains");
}

int main() {
    test_allows_up_to_the_limit_then_blocks();
    test_distinct_clients_have_separate_buckets();
    test_old_requests_fall_out_of_the_window();
    test_idle_clients_are_evicted();

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }

    std::cout << "rate limiter tests passed\n";
    return 0;
}
