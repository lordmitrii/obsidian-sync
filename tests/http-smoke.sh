#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
CLIENT_BINARY="${OBSIDIAN_SYNC_CLIENT_BIN:-$REPO_ROOT/build/obsidian-sync-client}"
SERVER_BINARY="${OBSIDIAN_SYNC_SERVER_BIN:-$REPO_ROOT/build/obsidian-sync-server}"
TOKEN="test-token"
PORT="${OBSIDIAN_SYNC_TEST_PORT:-19080}"
BASE_URL="http://127.0.0.1:$PORT"

if [[ ! -x "$CLIENT_BINARY" ]]; then
    echo "Missing executable: $CLIENT_BINARY" >&2
    echo "Build first with: cmake --build build" >&2
    exit 1
fi

if [[ ! -x "$SERVER_BINARY" ]]; then
    echo "Missing executable: $SERVER_BINARY" >&2
    echo "Build first with: cmake --build build" >&2
    exit 1
fi

TMP_ROOT="$(mktemp -d)"
SERVER_PID=""
trap '[[ -n "$SERVER_PID" ]] && kill "$SERVER_PID" 2>/dev/null || true; rm -rf "$TMP_ROOT"' EXIT

fail() {
    echo "FAIL: $*" >&2
    exit 1
}

assert_file_content() {
    local path="$1"
    local expected="$2"

    [[ -f "$path" ]] || fail "expected file to exist: $path"
    [[ "$(cat "$path")" == "$expected" ]] || fail "unexpected content in $path"
}

assert_missing() {
    local path="$1"

    [[ ! -e "$path" ]] || fail "expected path to be missing: $path"
}

start_server() {
    local root="$1"
    local log="$2"

    OBSIDIAN_SYNC_TOKEN="$TOKEN" "$SERVER_BINARY" \
        --server-root "$root" \
        --server-port "$PORT" >"$log" 2>&1 &
    SERVER_PID="$!"

    for _ in {1..50}; do
        if curl --max-time 1 -s -o /dev/null -w '%{http_code}' \
            -H "Authorization: Bearer $TOKEN" "$BASE_URL/manifest" | grep -qx '200'; then
            return
        fi

        sleep 0.1
    done

    cat "$log" >&2 || true
    fail "server did not become ready"
}

stop_server() {
    if [[ -n "$SERVER_PID" ]]; then
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
        SERVER_PID=""
    fi
}

expect_http_code() {
    local expected="$1"
    shift

    local actual
    actual="$(curl --max-time 5 -s -o /dev/null -w '%{http_code}' "$@")"
    [[ "$actual" == "$expected" ]] || fail "expected HTTP $expected, got $actual"
}

test_server_requires_token_env() {
    local root="$TMP_ROOT/no-token-root"
    mkdir -p "$root"

    if env -u OBSIDIAN_SYNC_TOKEN "$SERVER_BINARY" --server-root "$root" --server-port "$PORT" \
        >"$TMP_ROOT/no-token.log" 2>&1; then
        fail "server started without OBSIDIAN_SYNC_TOKEN"
    fi

    grep -q "OBSIDIAN_SYNC_TOKEN is required" "$TMP_ROOT/no-token.log" ||
        fail "missing token error did not explain OBSIDIAN_SYNC_TOKEN"

    echo "PASS: server requires token env"
}

test_auth_and_limits() {
    local root="$TMP_ROOT/auth-root"
    mkdir -p "$root"
    printf 'hello' >"$root/note.md"

    start_server "$root" "$TMP_ROOT/auth-server.log"

    expect_http_code 401 "$BASE_URL/manifest"
    expect_http_code 401 -H "Authorization: Bearer wrong" "$BASE_URL/manifest"
    expect_http_code 200 -H "Authorization: Bearer $TOKEN" "$BASE_URL/manifest"

    dd if=/dev/zero of="$TMP_ROOT/too-large.bin" bs=1M count=26 status=none
    expect_http_code 413 -X PUT -H "Authorization: Bearer $TOKEN" \
        --data-binary "@$TMP_ROOT/too-large.bin" "$BASE_URL/file?path=too-large.bin"

    local saw_429=0
    for _ in {1..130}; do
        local code
        code="$(curl --max-time 5 -s -o /dev/null -w '%{http_code}' \
            -H "Authorization: Bearer $TOKEN" "$BASE_URL/manifest")"

        if [[ "$code" == "429" ]]; then
            saw_429=1
            break
        fi
    done

    [[ "$saw_429" == "1" ]] || fail "rate limit did not return 429"

    stop_server
    echo "PASS: auth, upload limit, and rate limit"
}

test_client_sync_and_state() {
    local dir="$TMP_ROOT/sync"
    mkdir -p "$dir/base" "$dir/local" "$dir/remote"

    printf 'base' >"$dir/base/upload.md"
    printf 'base' >"$dir/base/download.md"
    printf 'base' >"$dir/base/delete-remote.md"
    printf 'base' >"$dir/base/delete-local.md"
    printf 'base' >"$dir/base/conflict.md"

    cp -a "$dir/base/." "$dir/local"
    cp -a "$dir/base/." "$dir/remote"

    printf 'local-edit' >"$dir/local/upload.md"
    printf 'remote-edit' >"$dir/remote/download.md"
    rm "$dir/local/delete-remote.md"
    rm "$dir/remote/delete-local.md"
    printf 'local-conflict' >"$dir/local/conflict.md"
    printf 'remote-conflict' >"$dir/remote/conflict.md"

    "$CLIENT_BINARY" --vault "$dir/base" --state "$dir/state.db" >/dev/null
    start_server "$dir/remote" "$TMP_ROOT/sync-server.log"

    if env -u OBSIDIAN_SYNC_TOKEN "$CLIENT_BINARY" \
        --local-root "$dir/local" \
        --remote-url "$BASE_URL" \
        --state "$dir/state.db" \
        --apply >"$dir/missing-token.log" 2>&1; then
        fail "HTTP client synced without OBSIDIAN_SYNC_TOKEN"
    fi

    OBSIDIAN_SYNC_TOKEN="$TOKEN" "$CLIENT_BINARY" \
        --local-root "$dir/local" \
        --remote-url "$BASE_URL" \
        --state "$dir/state.db" \
        --apply >"$dir/plan.txt"

    grep -Fx "upload upload.md" "$dir/plan.txt" >/dev/null || fail "missing upload action"
    grep -Fx "download download.md" "$dir/plan.txt" >/dev/null || fail "missing download action"
    grep -Fx "delete-remote delete-remote.md" "$dir/plan.txt" >/dev/null ||
        fail "missing delete-remote action"
    grep -Fx "delete-local delete-local.md" "$dir/plan.txt" >/dev/null ||
        fail "missing delete-local action"
    grep -Fx "conflict conflict.md" "$dir/plan.txt" >/dev/null || fail "missing conflict action"

    assert_file_content "$dir/remote/upload.md" "local-edit"
    assert_file_content "$dir/local/download.md" "remote-edit"
    assert_missing "$dir/remote/delete-remote.md"
    assert_missing "$dir/local/delete-local.md"
    assert_file_content "$dir/local/conflict.md" "local-conflict"
    assert_file_content "$dir/local/conflict.conflict-remote.md" "remote-conflict"

    OBSIDIAN_SYNC_TOKEN="$TOKEN" "$CLIENT_BINARY" \
        --local-root "$dir/local" \
        --remote-url "$BASE_URL" \
        --state "$dir/state.db" >"$dir/second-plan.txt"

    grep -Fx "conflict conflict.md" "$dir/second-plan.txt" >/dev/null ||
        fail "conflict should remain unresolved"
    grep -Fx "unchanged download.md" "$dir/second-plan.txt" >/dev/null ||
        fail "download should be marked synced"
    grep -Fx "unchanged upload.md" "$dir/second-plan.txt" >/dev/null ||
        fail "upload should be marked synced"

    stop_server
    echo "PASS: HTTP sync applies and updates state"
}

test_server_requires_token_env
test_auth_and_limits
test_client_sync_and_state

echo "All HTTP smoke tests passed"
