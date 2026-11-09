#include "object_store.hpp"

#include <filesystem>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

static int failures = 0;

static void expect(bool condition, const std::string &case_name) {
    if (!condition) {
        std::cerr << "FAIL [" << case_name << "]\n";
        ++failures;
    }
}

static fs::path fresh_dir(const std::string &name) {
    const auto dir = fs::temp_directory_path() / ("obsidian-sync-object-store-test-" + name);
    fs::remove_all(dir);
    return dir;
}

static void test_put_then_get_round_trips() {
    ObjectStore store(fresh_dir("round-trip"));
    std::string hash = store.put("hello world\n");

    expect(store.has(hash), "put_then_get (has)");
    auto loaded = store.get(hash);
    expect(loaded.has_value() && *loaded == "hello world\n", "put_then_get (content)");
}

static void test_get_missing_returns_nullopt() {
    ObjectStore store(fresh_dir("missing"));
    expect(!store.get("not-a-real-hash").has_value(), "get_missing_returns_nullopt");
}

static void test_put_is_idempotent_by_content() {
    ObjectStore store(fresh_dir("idempotent"));
    std::string hash_a = store.put("same content");
    std::string hash_b = store.put("same content");
    expect(hash_a == hash_b, "put_is_idempotent_by_content");
}

static void test_prune_keeps_only_listed_hashes() {
    fs::path dir = fresh_dir("prune");
    ObjectStore store(dir);

    std::string keep_hash = store.put("keep me");
    std::string drop_hash = store.put("drop me");

    store.prune({keep_hash});

    expect(store.has(keep_hash), "prune_keeps_only_listed_hashes (keeps)");
    expect(!store.has(drop_hash), "prune_keeps_only_listed_hashes (drops)");
}

static void test_prune_on_nonexistent_directory_is_a_no_op() {
    ObjectStore store(fresh_dir("never-written"));
    store.prune({});
}

int main() {
    test_put_then_get_round_trips();
    test_get_missing_returns_nullopt();
    test_put_is_idempotent_by_content();
    test_prune_keeps_only_listed_hashes();
    test_prune_on_nonexistent_directory_is_a_no_op();

    if (failures == 0) {
        std::cout << "All object store tests passed\n";
        return 0;
    }

    std::cerr << failures << " test case(s) failed\n";
    return 1;
}
