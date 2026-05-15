#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BINARY="${OBSIDIAN_SYNC_CLIENT_BIN:-$REPO_ROOT/build/obsidian-sync-client}"

if [[ ! -x "$BINARY" ]]; then
    echo "Missing executable: $BINARY" >&2
    echo "Build first with: cmake --build build" >&2
    exit 1
fi

TMP_ROOT="$(mktemp -d)"
trap 'rm -rf "$TMP_ROOT"' EXIT

pass_count=0

fail() {
    echo "FAIL: $*" >&2
    exit 1
}

assert_file_content() {
    local path="$1"
    local expected="$2"

    [[ -f "$path" ]] || fail "expected file to exist: $path"

    local actual
    actual="$(cat "$path")"

    [[ "$actual" == "$expected" ]] ||
        fail "unexpected content in $path: expected '$expected', got '$actual'"
}

assert_missing() {
    local path="$1"

    [[ ! -e "$path" ]] || fail "expected path to be missing: $path"
}

assert_plan_contains() {
    local plan="$1"
    local expected="$2"

    grep -Fx "$expected" "$plan" >/dev/null ||
        fail "expected plan line '$expected' in $plan"
}

new_case() {
    local name="$1"
    local dir="$TMP_ROOT/$name"

    mkdir -p "$dir/base" "$dir/local" "$dir/remote"
    printf '%s\n' "$dir"
}

seed_base() {
    local dir="$1"

    "$BINARY" --vault "$dir/base" --state "$dir/state.db" >/dev/null
}

run_sync() {
    local dir="$1"
    local plan="$dir/plan.txt"

    "$BINARY" \
        --local-root "$dir/local" \
        --remote-root "$dir/remote" \
        --state "$dir/state.db" \
        --apply >"$plan"

    printf '%s\n' "$plan"
}

pass() {
    local name="$1"

    pass_count=$((pass_count + 1))
    echo "PASS: $name"
}

case_new_local_uploads() {
    local dir
    dir="$(new_case new-local-uploads)"

    printf 'hello-local' >"$dir/local/note.md"

    seed_base "$dir"
    local plan
    plan="$(run_sync "$dir")"

    assert_plan_contains "$plan" "upload note.md"
    assert_file_content "$dir/remote/note.md" "hello-local"
    pass "new local file uploads"
}

case_new_remote_downloads() {
    local dir
    dir="$(new_case new-remote-downloads)"

    printf 'hello-remote' >"$dir/remote/note.md"

    seed_base "$dir"
    local plan
    plan="$(run_sync "$dir")"

    assert_plan_contains "$plan" "download note.md"
    assert_file_content "$dir/local/note.md" "hello-remote"
    pass "new remote file downloads"
}

case_local_edit_uploads() {
    local dir
    dir="$(new_case local-edit-uploads)"

    printf 'base' >"$dir/base/note.md"
    cp "$dir/base/note.md" "$dir/local/note.md"
    cp "$dir/base/note.md" "$dir/remote/note.md"
    printf 'local-edit' >"$dir/local/note.md"

    seed_base "$dir"
    local plan
    plan="$(run_sync "$dir")"

    assert_plan_contains "$plan" "upload note.md"
    assert_file_content "$dir/remote/note.md" "local-edit"
    pass "local edit uploads"
}

case_remote_edit_downloads() {
    local dir
    dir="$(new_case remote-edit-downloads)"

    printf 'base' >"$dir/base/note.md"
    cp "$dir/base/note.md" "$dir/local/note.md"
    cp "$dir/base/note.md" "$dir/remote/note.md"
    printf 'remote-edit' >"$dir/remote/note.md"

    seed_base "$dir"
    local plan
    plan="$(run_sync "$dir")"

    assert_plan_contains "$plan" "download note.md"
    assert_file_content "$dir/local/note.md" "remote-edit"
    pass "remote edit downloads"
}

case_both_edit_conflicts() {
    local dir
    dir="$(new_case both-edit-conflicts)"

    printf 'base' >"$dir/base/note.md"
    cp "$dir/base/note.md" "$dir/local/note.md"
    cp "$dir/base/note.md" "$dir/remote/note.md"
    printf 'local-edit' >"$dir/local/note.md"
    printf 'remote-edit' >"$dir/remote/note.md"

    seed_base "$dir"
    local plan
    plan="$(run_sync "$dir")"

    assert_plan_contains "$plan" "conflict note.md"
    assert_file_content "$dir/local/note.md" "local-edit"
    assert_file_content "$dir/local/note.conflict-remote.md" "remote-edit"
    pass "both edit creates conflict"
}

case_local_delete_deletes_remote() {
    local dir
    dir="$(new_case local-delete-deletes-remote)"

    printf 'base' >"$dir/base/note.md"
    cp "$dir/base/note.md" "$dir/remote/note.md"

    seed_base "$dir"
    local plan
    plan="$(run_sync "$dir")"

    assert_plan_contains "$plan" "delete-remote note.md"
    assert_missing "$dir/remote/note.md"
    pass "local delete deletes remote"
}

case_remote_delete_deletes_local() {
    local dir
    dir="$(new_case remote-delete-deletes-local)"

    printf 'base' >"$dir/base/note.md"
    cp "$dir/base/note.md" "$dir/local/note.md"

    seed_base "$dir"
    local plan
    plan="$(run_sync "$dir")"

    assert_plan_contains "$plan" "delete-local note.md"
    assert_missing "$dir/local/note.md"
    pass "remote delete deletes local"
}

case_new_local_uploads
case_new_remote_downloads
case_local_edit_uploads
case_remote_edit_downloads
case_both_edit_conflicts
case_local_delete_deletes_remote
case_remote_delete_deletes_local

echo "All $pass_count manual sync cases passed"
