#include "local_remote_backend.hpp"
#include "sync_service.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>

namespace fs = std::filesystem;

static int failures = 0;

static void expect(bool condition, const std::string &case_name) {
    if (!condition) {
        std::cerr << "FAIL [" << case_name << "]\n";
        ++failures;
    }
}

static void write_file(const fs::path &path, const std::string &content) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << content;
}

static std::string read_file(const fs::path &path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

struct Fixture {
    fs::path root;
    fs::path local;
    fs::path remote;
    std::string state;

    explicit Fixture(const std::string &name)
        : root(fs::temp_directory_path() / ("obsidian-sync-service-test-" + name)),
          local(root / "local"),
          remote(root / "remote"),
          state((root / "state.db").string()) {
        fs::remove_all(root);
        fs::create_directories(local);
        fs::create_directories(remote);
    }

    ~Fixture() { fs::remove_all(root); }

    RunStatus sync() {
        LocalRemoteBackend backend(remote);
        SyncService service(local, state, backend, true);
        return service.run_once();
    }

    RunStatus sync_with_limits(DeleteSafetyLimits limits) {
        LocalRemoteBackend backend(remote);
        SyncService service(local, state, backend, true, limits);
        return service.run_once();
    }
};

// Simulates the user saving a local file mid-run: load_manifest() is called
// after the run's local scan but before it applies actions and records base
// state, so writing the new content there mimics an edit landing in that gap.
class MidRunEditingBackend : public LocalRemoteBackend {
  public:
    MidRunEditingBackend(const fs::path &remote_root, fs::path edit_path, std::string new_content)
        : LocalRemoteBackend(remote_root),
          edit_path_(std::move(edit_path)),
          new_content_(std::move(new_content)) {}

    Manifest load_manifest() override {
        if (!new_content_.empty()) {
            write_file(edit_path_, new_content_);
            new_content_.clear();
        }

        return LocalRemoteBackend::load_manifest();
    }

  private:
    fs::path edit_path_;
    std::string new_content_;
};

static void test_first_sync_uploads_and_downloads() {
    Fixture f("first");
    write_file(f.local / "mine.md", "local note");
    write_file(f.remote / "theirs.md", "remote note");

    f.sync();

    expect(read_file(f.remote / "mine.md") == "local note", "first_sync_uploads_local_file");
    expect(read_file(f.local / "theirs.md") == "remote note", "first_sync_downloads_remote_file");
}

static void test_dry_run_changes_nothing() {
    Fixture f("dry");
    write_file(f.local / "mine.md", "local note");

    LocalRemoteBackend backend(f.remote);
    SyncService service(f.local, f.state, backend, false);
    service.run_once();

    expect(!fs::exists(f.remote / "mine.md"), "dry_run_does_not_upload");
}

static void test_local_edit_propagates_after_base_recorded() {
    Fixture f("edit");
    write_file(f.local / "a.md", "v1");
    f.sync();

    write_file(f.local / "a.md", "v2 with more text");
    f.sync();

    expect(read_file(f.remote / "a.md") == "v2 with more text", "local_edit_uploaded");
}

static void test_local_delete_propagates() {
    Fixture f("delete");
    write_file(f.local / "a.md", "v1");
    f.sync();

    fs::remove(f.local / "a.md");
    f.sync();

    expect(!fs::exists(f.remote / "a.md"), "local_delete_removes_remote");
}

static void test_remote_delete_propagates() {
    Fixture f("remote-delete");
    write_file(f.local / "a.md", "v1");
    f.sync();

    fs::remove(f.remote / "a.md");
    f.sync();

    expect(!fs::exists(f.local / "a.md"), "remote_delete_removes_local");
    expect(read_file(f.local / ".trash" / "a.md") == "v1", "remote_delete_moves_local_to_trash");
}

static void test_delete_local_trash_keeps_directory_structure() {
    Fixture f("trash-nested");
    write_file(f.local / "notes" / "a.md", "v1");
    f.sync();

    fs::remove(f.remote / "notes" / "a.md");
    f.sync();

    expect(!fs::exists(f.local / "notes" / "a.md"), "nested_delete_removes_local");
    expect(read_file(f.local / ".trash" / "notes" / "a.md") == "v1",
           "nested_delete_moves_to_trash_keeping_structure");
}

static void test_repeated_delete_of_same_path_does_not_clobber_trash() {
    Fixture f("trash-repeat");
    write_file(f.local / "a.md", "v1");
    f.sync();
    fs::remove(f.remote / "a.md");
    f.sync();

    write_file(f.local / "a.md", "v2");
    write_file(f.remote / "a.md", "v2");
    f.sync();
    fs::remove(f.remote / "a.md");
    f.sync();

    expect(read_file(f.local / ".trash" / "a.md") == "v1", "first_trashed_copy_is_untouched");
    expect(read_file(f.local / ".trash" / "a.2.md") == "v2", "second_trashed_copy_gets_a_number");
}

static void test_conflict_keeps_local_and_saves_remote_copy() {
    Fixture f("conflict");
    write_file(f.local / "a.md", "base");
    f.sync();

    write_file(f.local / "a.md", "local edit");
    write_file(f.remote / "a.md", "remote edit!");
    RunStatus status = f.sync();

    expect(status == RunStatus::ConflictsPending, "conflict_reports_conflicts_pending");
    expect(read_file(f.local / "a.md") == "local edit", "conflict_keeps_local");
    expect(read_file(f.remote / "a.md") == "remote edit!", "conflict_keeps_remote");
    expect(read_file(f.local / "a.conflict-remote.md") == "remote edit!",
           "conflict_saves_remote_copy");
}

static void test_repeated_conflict_does_not_fail_forever() {
    Fixture f("repeated-conflict");
    write_file(f.local / "a.md", "base");
    f.sync();

    write_file(f.local / "a.md", "local edit");
    write_file(f.remote / "a.md", "remote edit!");
    f.sync();

    // Base state doesn't move forward on conflict, so this replans the same
    // conflict on the next run, same as --watch polling every interval.
    RunStatus status = f.sync();

    expect(status == RunStatus::ConflictsPending, "unchanged_conflict_reports_conflicts_pending");
    expect(read_file(f.local / "a.conflict-remote.md") == "remote edit!",
           "unchanged_conflict_copy_still_matches_remote");
}

static void test_conflict_gets_a_numbered_copy_when_remote_changes_again() {
    Fixture f("renumbered-conflict");
    write_file(f.local / "a.md", "base");
    f.sync();

    write_file(f.local / "a.md", "local edit");
    write_file(f.remote / "a.md", "remote edit!");
    f.sync();

    write_file(f.remote / "a.md", "remote edit 2!");
    RunStatus status = f.sync();

    expect(status == RunStatus::ConflictsPending, "renumbered_conflict_reports_conflicts_pending");
    expect(read_file(f.local / "a.conflict-remote.md") == "remote edit!",
           "first_conflict_copy_is_untouched");
    expect(read_file(f.local / "a.conflict-remote.2.md") == "remote edit 2!",
           "second_conflict_copy_gets_a_number");
}

static void test_one_failed_action_does_not_block_the_others() {
    Fixture f("partial-failure");
    write_file(f.local / "good.md", "v1");
    write_file(f.local / "bad.md", "v1");
    // A directory in place of the remote file makes the upload fail.
    fs::create_directories(f.remote / "bad.md");

    RunStatus status = f.sync();

    expect(status == RunStatus::ActionsFailed, "run_once_reports_failure_when_an_action_fails");
    expect(read_file(f.remote / "good.md") == "v1", "other_upload_still_applied");
    expect(fs::is_directory(f.remote / "bad.md"), "failed_upload_left_remote_untouched");

    fs::remove_all(f.remote / "bad.md");
    RunStatus retried_status = f.sync();

    expect(retried_status == RunStatus::Success, "failed_action_is_retried_next_run");
    expect(read_file(f.remote / "bad.md") == "v1", "retried_upload_eventually_applied");
}

static void test_mass_delete_is_blocked_by_default() {
    Fixture f("mass-delete");

    for (int i = 0; i < 10; ++i) {
        write_file(f.local / ("note" + std::to_string(i) + ".md"), "v1");
    }

    f.sync();

    for (int i = 0; i < 10; ++i) {
        fs::remove(f.local / ("note" + std::to_string(i) + ".md"));
    }

    RunStatus status = f.sync();

    expect(status == RunStatus::DeletesBlocked, "mass_delete_reports_deletes_blocked");

    for (int i = 0; i < 10; ++i) {
        expect(fs::exists(f.remote / ("note" + std::to_string(i) + ".md")),
               "mass_delete_did_not_touch_remote_" + std::to_string(i));
    }
}

static void test_mass_delete_proceeds_with_force_deletes() {
    Fixture f("mass-delete-forced");

    for (int i = 0; i < 10; ++i) {
        write_file(f.local / ("note" + std::to_string(i) + ".md"), "v1");
    }

    f.sync();

    for (int i = 0; i < 10; ++i) {
        fs::remove(f.local / ("note" + std::to_string(i) + ".md"));
    }

    RunStatus status = f.sync_with_limits(DeleteSafetyLimits{true, 50, 50.0});

    expect(status == RunStatus::Success, "forced_mass_delete_succeeds");
    expect(!fs::exists(f.remote / "note0.md"), "forced_mass_delete_removes_remote");
}

static void test_small_delete_is_not_blocked() {
    Fixture f("small-delete");

    for (int i = 0; i < 10; ++i) {
        write_file(f.local / ("note" + std::to_string(i) + ".md"), "v1");
    }

    f.sync();

    fs::remove(f.local / "note0.md");
    RunStatus status = f.sync();

    expect(status == RunStatus::Success, "small_delete_not_blocked");
    expect(!fs::exists(f.remote / "note0.md"), "small_delete_removes_remote");
}

// 10 files is below --max-deletes' default (50), so this would slip through
// the count check, and it is not "too small" for the percent check to apply
// so an empty remote manifest still needs its own, unconditional check.
static void test_empty_remote_manifest_is_blocked_even_under_max_deletes() {
    Fixture f("empty-remote-under-max-deletes");

    for (int i = 0; i < 10; ++i) {
        write_file(f.local / ("note" + std::to_string(i) + ".md"), "v1");
    }

    f.sync();

    for (int i = 0; i < 10; ++i) {
        fs::remove(f.remote / ("note" + std::to_string(i) + ".md"));
    }

    RunStatus status = f.sync();

    expect(status == RunStatus::DeletesBlocked, "empty_remote_manifest_reports_deletes_blocked");

    for (int i = 0; i < 10; ++i) {
        expect(fs::exists(f.local / ("note" + std::to_string(i) + ".md")),
               "empty_remote_manifest_kept_local_" + std::to_string(i));
    }
}

static void test_empty_remote_manifest_proceeds_with_force_deletes() {
    Fixture f("empty-remote-forced");

    for (int i = 0; i < 10; ++i) {
        write_file(f.local / ("note" + std::to_string(i) + ".md"), "v1");
    }

    f.sync();

    for (int i = 0; i < 10; ++i) {
        fs::remove(f.remote / ("note" + std::to_string(i) + ".md"));
    }

    RunStatus status = f.sync_with_limits(DeleteSafetyLimits{true, 50, 50.0});

    expect(status == RunStatus::Success, "empty_remote_manifest_forced_proceeds");

    for (int i = 0; i < 10; ++i) {
        expect(fs::exists(f.local / ".trash" / ("note" + std::to_string(i) + ".md")),
               "empty_remote_manifest_forced_trashed_" + std::to_string(i));
    }
}

// Below the same threshold, an emptied remote is treated like any other
// small delete (see test_small_delete_is_not_blocked) rather than refused,
// since a tiny vault legitimately losing all of its (few) files is common.
static void test_empty_remote_manifest_on_tiny_vault_is_not_blocked() {
    Fixture f("empty-remote-tiny");

    for (int i = 0; i < 3; ++i) {
        write_file(f.local / ("note" + std::to_string(i) + ".md"), "v1");
    }

    f.sync();

    for (int i = 0; i < 3; ++i) {
        fs::remove(f.remote / ("note" + std::to_string(i) + ".md"));
    }

    RunStatus status = f.sync();

    expect(status == RunStatus::Success, "empty_remote_manifest_tiny_vault_not_blocked");

    for (int i = 0; i < 3; ++i) {
        expect(fs::exists(f.local / ".trash" / ("note" + std::to_string(i) + ".md")),
               "empty_remote_manifest_tiny_vault_trashed_" + std::to_string(i));
    }
}

static void test_edit_during_sync_run_is_not_lost() {
    Fixture f("mid-run-edit");
    write_file(f.local / "a.md", "v1");
    f.sync();

    MidRunEditingBackend backend(f.remote, f.local / "a.md", "v2 mid-run edit");
    SyncService service(f.local, f.state, backend, true);
    service.run_once();

    expect(read_file(f.remote / "a.md") == "v1", "mid_run_edit_remote_untouched_by_that_run");

    f.sync();

    expect(read_file(f.remote / "a.md") == "v2 mid-run edit", "mid_run_edit_is_uploaded_not_lost");
    expect(read_file(f.local / "a.md") == "v2 mid-run edit", "mid_run_edit_survives_locally");
}

int main() {
    test_first_sync_uploads_and_downloads();
    test_dry_run_changes_nothing();
    test_local_edit_propagates_after_base_recorded();
    test_local_delete_propagates();
    test_remote_delete_propagates();
    test_conflict_keeps_local_and_saves_remote_copy();
    test_repeated_conflict_does_not_fail_forever();
    test_conflict_gets_a_numbered_copy_when_remote_changes_again();
    test_one_failed_action_does_not_block_the_others();
    test_mass_delete_is_blocked_by_default();
    test_mass_delete_proceeds_with_force_deletes();
    test_small_delete_is_not_blocked();
    test_empty_remote_manifest_is_blocked_even_under_max_deletes();
    test_empty_remote_manifest_proceeds_with_force_deletes();
    test_empty_remote_manifest_on_tiny_vault_is_not_blocked();
    test_delete_local_trash_keeps_directory_structure();
    test_repeated_delete_of_same_path_does_not_clobber_trash();
    test_edit_during_sync_run_is_not_lost();

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }

    std::cout << "sync service tests passed\n";
    return 0;
}
