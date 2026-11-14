#include "config.hpp"

#include <iostream>
#include <stdexcept>
#include <vector>

static int failures = 0;

static void expect(bool condition, const std::string &case_name) {
    if (!condition) {
        std::cerr << "FAIL [" << case_name << "]\n";
        ++failures;
    }
}

static Config parse(std::vector<std::string> args) {
    std::vector<char *> argv;
    argv.push_back(const_cast<char *>("obsidian-sync"));

    for (auto &arg : args) {
        argv.push_back(const_cast<char *>(arg.c_str()));
    }

    return parse_args(static_cast<int>(argv.size()), argv.data());
}

static bool throws_with(std::vector<std::string> args, const std::string &expected_message) {
    try {
        parse(std::move(args));
    } catch (const std::runtime_error &error) {
        return std::string(error.what()) == expected_message;
    }

    return false;
}

static void test_rejects_trailing_garbage_in_port() {
    expect(throws_with({"--server-root", "/tmp", "--server-port", "8080abc"},
                       "--server-port must be a number"),
           "port_trailing_garbage");
    expect(throws_with({"--server-root", "/tmp", "--server-port", "abc"},
                       "--server-port must be a number"),
           "port_non_numeric");
}

static void test_rejects_trailing_garbage_in_interval() {
    expect(throws_with({"--local-root", "/tmp", "--remote-root", "/tmp", "--apply", "--watch",
                        "--interval", "30x"},
                       "--interval must be a number"),
           "interval_trailing_garbage");
}

static void test_accepts_valid_numbers() {
    Config config = parse({"--server-root", "/tmp", "--server-port", "8080"});
    expect(config.server_port == 8080, "port_accepts_plain_number");

    Config with_interval = parse({"--local-root", "/tmp", "--remote-root", "/tmp", "--apply",
                                  "--watch", "--interval", "45"});
    expect(with_interval.watch_interval_seconds == 45, "interval_accepts_plain_number");
}

static void test_trusted_proxy() {
    Config config = parse({"--server-root", "/tmp", "--trusted-proxy", "127.0.0.1"});
    expect(config.trusted_proxy == "127.0.0.1", "trusted_proxy_is_captured");

    Config without = parse({"--server-root", "/tmp"});
    expect(without.trusted_proxy.empty(), "trusted_proxy_defaults_to_empty");

    expect(throws_with({"--server-root", "/tmp", "--trusted-proxy"},
                       "--trusted-proxy requires an IP address"),
           "trusted_proxy_requires_a_value");
}

static void test_insecure_http_and_cacert() {
    Config config = parse({"--local-root", "/tmp", "--remote-url", "http://example.com",
                           "--insecure-http", "--cacert", "/tmp/ca.pem"});
    expect(config.insecure_http, "insecure_http_flag_is_captured");
    expect(config.cacert_path == "/tmp/ca.pem", "cacert_path_is_captured");

    Config without = parse({"--local-root", "/tmp", "--remote-url", "https://example.com"});
    expect(!without.insecure_http, "insecure_http_defaults_to_false");
    expect(without.cacert_path.empty(), "cacert_path_defaults_to_empty");

    expect(throws_with({"--local-root", "/tmp", "--remote-url", "https://example.com", "--cacert"},
                       "--cacert requires a path"),
           "cacert_requires_a_value");
}

static void test_delete_safety_limits() {
    Config defaults = parse({"--local-root", "/tmp", "--remote-root", "/tmp"});
    expect(!defaults.force_deletes, "force_deletes_defaults_to_false");
    expect(defaults.max_delete_count == 50, "max_delete_count_defaults_to_50");
    expect(defaults.max_delete_percent == 50.0, "max_delete_percent_defaults_to_50");

    Config config = parse({"--local-root", "/tmp", "--remote-root", "/tmp", "--force-deletes",
                           "--max-deletes", "10", "--max-delete-percent", "25.5"});
    expect(config.force_deletes, "force_deletes_flag_is_captured");
    expect(config.max_delete_count == 10, "max_delete_count_is_captured");
    expect(config.max_delete_percent == 25.5, "max_delete_percent_is_captured");

    expect(throws_with({"--local-root", "/tmp", "--remote-root", "/tmp", "--max-deletes", "abc"},
                       "--max-deletes must be a number"),
           "max_deletes_rejects_non_numeric");
    expect(throws_with(
               {"--local-root", "/tmp", "--remote-root", "/tmp", "--max-delete-percent", "150"},
               "--max-delete-percent must be between 0 and 100"),
           "max_delete_percent_rejects_out_of_range");
}

static void test_trash_retention_days() {
    Config defaults = parse({"--local-root", "/tmp", "--remote-root", "/tmp"});
    expect(defaults.trash_retention_days == 30, "trash_retention_days_defaults_to_30");

    Config config =
        parse({"--local-root", "/tmp", "--remote-root", "/tmp", "--trash-retention-days", "7"});
    expect(config.trash_retention_days == 7, "trash_retention_days_is_captured");

    expect(throws_with(
               {"--local-root", "/tmp", "--remote-root", "/tmp", "--trash-retention-days", "abc"},
               "--trash-retention-days must be a number"),
           "trash_retention_days_rejects_non_numeric");
}

static void test_sync_config_preset() {
    Config defaults = parse({"--local-root", "/tmp", "--remote-root", "/tmp"});
    expect(defaults.sync_config == SyncConfigPreset::All, "sync_config_defaults_to_all");

    Config none = parse({"--local-root", "/tmp", "--remote-root", "/tmp", "--sync-config", "none"});
    expect(none.sync_config == SyncConfigPreset::None, "sync_config_accepts_none");

    Config core = parse({"--local-root", "/tmp", "--remote-root", "/tmp", "--sync-config", "core"});
    expect(core.sync_config == SyncConfigPreset::Core, "sync_config_accepts_core");

    expect(throws_with({"--local-root", "/tmp", "--remote-root", "/tmp", "--sync-config", "bogus"},
                       "--sync-config must be one of: none, core, all"),
           "sync_config_rejects_unknown_preset");
}

static void test_on_conflict_policy() {
    Config defaults = parse({"--local-root", "/tmp", "--remote-root", "/tmp"});
    expect(defaults.on_conflict == OnConflictPolicy::Copy, "on_conflict_defaults_to_copy");

    Config merge =
        parse({"--local-root", "/tmp", "--remote-root", "/tmp", "--on-conflict", "merge"});
    expect(merge.on_conflict == OnConflictPolicy::Merge, "on_conflict_accepts_merge");

    Config keep_local =
        parse({"--local-root", "/tmp", "--remote-root", "/tmp", "--on-conflict", "keep-local"});
    expect(keep_local.on_conflict == OnConflictPolicy::KeepLocal, "on_conflict_accepts_keep_local");

    Config keep_remote =
        parse({"--local-root", "/tmp", "--remote-root", "/tmp", "--on-conflict", "keep-remote"});
    expect(keep_remote.on_conflict == OnConflictPolicy::KeepRemote,
           "on_conflict_accepts_keep_remote");

    expect(throws_with({"--local-root", "/tmp", "--remote-root", "/tmp", "--on-conflict", "bogus"},
                       "--on-conflict must be one of: copy, merge, keep-local, keep-remote"),
           "on_conflict_rejects_unknown_policy");
}

static void test_quiet() {
    Config defaults = parse({"--vault", "/tmp"});
    expect(!defaults.quiet, "quiet_defaults_to_false");

    Config quiet = parse({"--vault", "/tmp", "--quiet"});
    expect(quiet.quiet, "quiet_accepts_flag");
}

static void test_status() {
    Config defaults = parse({"--vault", "/tmp"});
    expect(!defaults.status_output, "status_defaults_to_false");

    Config status = parse({"--vault", "/tmp", "--status"});
    expect(status.status_output, "status_accepts_flag");
}

static void test_conflicts() {
    Config defaults = parse({"--vault", "/tmp"});
    expect(!defaults.conflicts_output, "conflicts_defaults_to_false");

    Config conflicts = parse({"--vault", "/tmp", "--conflicts"});
    expect(conflicts.conflicts_output, "conflicts_accepts_flag");
}

static void test_conflicts_subcommands() {
    Config list = parse({"conflicts", "list", "--vault", "/tmp"});
    expect(list.conflicts_output, "conflicts_list_subcommand_sets_conflicts_output");
    expect(list.vault_path == "/tmp", "conflicts_list_subcommand_still_parses_other_flags");

    Config resolve =
        parse({"conflicts", "resolve", "note.md", "--vault", "/tmp", "--keep", "remote"});
    expect(resolve.resolve_conflict_path == "note.md",
           "conflicts_resolve_subcommand_captures_note_path");
    expect(resolve.conflict_resolution == ConflictResolution::KeepRemote,
           "conflicts_resolve_subcommand_still_parses_keep");

    expect(throws_with({"conflicts"}, "conflicts requires a subcommand: list or resolve"),
           "conflicts_requires_subcommand");
    expect(throws_with({"conflicts", "bogus", "--vault", "/tmp"},
                       "Unknown conflicts subcommand: bogus (expected list or resolve)"),
           "conflicts_rejects_unknown_subcommand");
    expect(throws_with({"conflicts", "resolve", "--vault", "/tmp"},
                       "conflicts resolve requires a note path"),
           "conflicts_resolve_requires_note_path");
}

static void test_history_subcommand() {
    Config history = parse({"history", "note.md", "--remote-url", "https://example.org"});
    expect(history.history_path == "note.md", "history_subcommand_captures_path");
    expect(history.remote_url == "https://example.org", "history_subcommand_still_parses_flags");

    expect(throws_with({"history"}, "history requires a path"), "history_requires_path");
    expect(
        throws_with({"history", "--remote-url", "https://example.org"}, "history requires a path"),
        "history_rejects_flag_in_place_of_path");
    expect(throws_with({"history", "note.md"}, "history, restore, and prune require --remote-url"),
           "history_requires_remote_url");
}

static void test_restore_subcommand() {
    Config by_hash =
        parse({"restore", "note.md", "--remote-url", "https://example.org", "--hash", "abc123"});
    expect(by_hash.restore_path == "note.md", "restore_subcommand_captures_path");
    expect(by_hash.restore_hash == "abc123", "restore_subcommand_parses_hash");

    Config by_time = parse({"restore", "note.md", "--remote-url", "https://example.org", "--at",
                            "2026-01-01T00:00:00Z"});
    expect(by_time.restore_at == "2026-01-01T00:00:00Z", "restore_subcommand_parses_at");

    expect(throws_with({"restore"}, "restore requires a path"), "restore_requires_path");
    expect(throws_with({"restore", "note.md", "--remote-url", "https://example.org"},
                       "restore requires --hash <hash> or --at <timestamp>"),
           "restore_requires_hash_or_at");
    expect(throws_with({"restore", "note.md", "--remote-url", "https://example.org", "--hash",
                        "abc", "--at", "2026-01-01T00:00:00Z"},
                       "restore takes --hash or --at, not both"),
           "restore_rejects_both_hash_and_at");
}

static void test_prune_subcommand() {
    Config prune = parse({"prune", "--remote-url", "https://example.org"});
    expect(prune.prune_remote, "prune_subcommand_sets_prune_remote");

    expect(throws_with({"prune"}, "history, restore, and prune require --remote-url"),
           "prune_requires_remote_url");
}

static void test_compare_manifests() {
    Config config = parse({"--compare-manifests", "a.json", "b.json"});
    expect(config.compare_manifests, "compare_manifests_flag_is_set");
    expect(config.compare_manifest_a == "a.json", "compare_manifests_captures_first_path");
    expect(config.compare_manifest_b == "b.json", "compare_manifests_captures_second_path");

    expect(throws_with({"--compare-manifests", "a.json"},
                       "--compare-manifests requires two manifest paths"),
           "compare_manifests_requires_two_paths");
}

static void test_watch_requires_apply() {
    expect(throws_with({"--local-root", "/tmp", "--remote-root", "/tmp", "--watch"},
                       "--watch requires --apply"),
           "watch_requires_apply");
}

static void test_resolve_conflict_requires_keep() {
    expect(throws_with({"--vault", "/tmp", "--resolve-conflict", "note.md"},
                       "--resolve-conflict requires --keep local|remote|merged"),
           "resolve_conflict_requires_keep");
}

static void test_missing_required_argument() {
    expect(throws_with({}, "Missing required argument: --vault, --server-root, "
                           "--compare-manifests, or --local-root with --remote-root/--remote-url"),
           "missing_required_argument");
}

static void test_unknown_argument() {
    expect(throws_with({"--vault", "/tmp", "--bogus-flag"}, "Unknown argument: --bogus-flag"),
           "unknown_argument_is_rejected");
}

static void test_server_port_range() {
    expect(throws_with({"--server-root", "/tmp", "--server-port", "0"},
                       "--server-port must be between 1 and 65535"),
           "server_port_rejects_zero");
    expect(throws_with({"--server-root", "/tmp", "--server-port", "65536"},
                       "--server-port must be between 1 and 65535"),
           "server_port_rejects_too_large");
}

static void test_interval_must_be_positive() {
    expect(throws_with({"--local-root", "/tmp", "--remote-root", "/tmp", "--apply", "--watch",
                        "--interval", "0"},
                       "--interval must be greater than zero"),
           "interval_rejects_zero");
}

static void test_max_deletes_rejects_negative() {
    expect(throws_with({"--local-root", "/tmp", "--remote-root", "/tmp", "--max-deletes", "-1"},
                       "--max-deletes must not be negative"),
           "max_deletes_rejects_negative");
}

static void test_on_conflict_cmd_and_manifest_file() {
    Config config = parse({"--vault", "/tmp", "--manifest-file", "out.json", "--on-conflict-cmd",
                           "notify-send conflict"});
    expect(config.manifest_file_path == "out.json", "manifest_file_path_is_captured");
    expect(config.on_conflict_cmd == "notify-send conflict", "on_conflict_cmd_is_captured");
}

static void test_help_and_version_short_circuit() {
    Config help = parse({"--vault", "/tmp", "--help"});
    expect(help.help, "help_flag_is_set");

    Config help_short = parse({"-h"});
    expect(help_short.help, "help_short_flag_is_set");

    Config version = parse({"--vault", "/tmp", "--version"});
    expect(version.version, "version_flag_is_set");
    expect(!version.help, "version_does_not_also_set_help");
}

int main() {
    test_rejects_trailing_garbage_in_port();
    test_rejects_trailing_garbage_in_interval();
    test_accepts_valid_numbers();
    test_trusted_proxy();
    test_insecure_http_and_cacert();
    test_delete_safety_limits();
    test_trash_retention_days();
    test_sync_config_preset();
    test_on_conflict_policy();
    test_quiet();
    test_status();
    test_conflicts();
    test_conflicts_subcommands();
    test_history_subcommand();
    test_restore_subcommand();
    test_prune_subcommand();
    test_compare_manifests();
    test_watch_requires_apply();
    test_resolve_conflict_requires_keep();
    test_missing_required_argument();
    test_unknown_argument();
    test_server_port_range();
    test_interval_must_be_positive();
    test_max_deletes_rejects_negative();
    test_on_conflict_cmd_and_manifest_file();
    test_help_and_version_short_circuit();

    if (failures == 0) {
        std::cout << "All config tests passed\n";
        return 0;
    }

    std::cerr << failures << " test case(s) failed\n";
    return 1;
}
