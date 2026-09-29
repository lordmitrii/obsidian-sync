#!/usr/bin/env bash
set -euo pipefail

# Measures how much the size/mtime hash cache (scan_file's known_hashes,
# see src/core/scanner.cpp) saves on a vault too big to want re-hashed every
# run. Generates a vault of --files files (default 10000), scans it once to
# populate state.db (no cache yet, every file gets hashed), then scans it
# again unchanged (every file should be a cache hit) and reports both
# timings.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
CLIENT_BINARY="${OBSIDIAN_SYNC_CLIENT_BIN:-$REPO_ROOT/build/obsidian-sync-client}"
FILE_COUNT="${OBSIDIAN_SYNC_BENCH_FILES:-10000}"

if [[ ! -x "$CLIENT_BINARY" ]]; then
    echo "Missing executable: $CLIENT_BINARY" >&2
    echo "Build first with: cmake --build build" >&2
    exit 1
fi

TMP_ROOT="$(mktemp -d)"
trap 'rm -rf "$TMP_ROOT"' EXIT

VAULT="$TMP_ROOT/vault"
STATE_DB="$TMP_ROOT/state.db"
mkdir -p "$VAULT"

echo "Generating $FILE_COUNT files in $VAULT..."
for ((i = 0; i < FILE_COUNT; i++)); do
    echo "note $i content $RANDOM" > "$VAULT/note-$i.md"
done

elapsed_ms() {
    local start_ns="$1"
    local end_ns="$2"
    echo $(( (end_ns - start_ns) / 1000000 ))
}

start_ns=$(date +%s%N)
"$CLIENT_BINARY" --vault "$VAULT" --state "$STATE_DB" >/dev/null
end_ns=$(date +%s%N)
cold_ms=$(elapsed_ms "$start_ns" "$end_ns")
echo "Cold scan (no cache, $FILE_COUNT files hashed): ${cold_ms}ms"

# The cache is skipped for files modified within the last second (see
# is_racy_mtime); make sure the vault's mtimes are safely in the past.
sleep 2

start_ns=$(date +%s%N)
"$CLIENT_BINARY" --vault "$VAULT" --state "$STATE_DB" >/dev/null
end_ns=$(date +%s%N)
warm_ms=$(elapsed_ms "$start_ns" "$end_ns")
echo "Warm scan (unchanged, cache hits expected): ${warm_ms}ms"

if [[ "$warm_ms" -gt 0 ]]; then
    echo "Speedup: $(( cold_ms / warm_ms ))x"
fi
