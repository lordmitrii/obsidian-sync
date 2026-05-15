#!/usr/bin/env bash
set -euo pipefail

# ── obsidian-sync client installer ───────────────────────────────────────────
# Syncs an entire folder (all vaults inside it) with one server root.
# Run from the repo root on any machine you want to sync from.
#
# Usage:
#   ./install-client.sh
#   OBSIDIAN_SYNC_TOKEN=xxx OBSIDIAN_SYNC_REMOTE_URL=https://... ./install-client.sh
# ─────────────────────────────────────────────────────────────────────────────

REMOTE_URL="${OBSIDIAN_SYNC_REMOTE_URL:-https://sync.utils.pemsoft.org}"
INSTALL_DIR="$HOME/.local/bin"
STATE_DIR="$HOME/.local/share/obsidian-sync"
OS=$(uname -s)

die()    { echo "error: $*" >&2; exit 1; }
info()   { echo "  $*"; }
header() { echo ""; echo "── $*"; }

# ── locate default obsidian folder ───────────────────────────────────────────
if [[ "$OS" == "Linux" ]]; then
    DEFAULT_ROOT="$HOME/Documents/Obsidian"
    OBSIDIAN_BIN=$(command -v obsidian 2>/dev/null || true)
    OBSIDIAN_APP=""
elif [[ "$OS" == "Darwin" ]]; then
    DEFAULT_ROOT="$HOME/Documents/Obsidian"
    OBSIDIAN_BIN=""
    OBSIDIAN_APP=$(mdfind "kMDItemCFBundleIdentifier == 'md.obsidian'" 2>/dev/null | head -1 || true)
else
    die "unsupported OS: $OS (Linux and macOS only)"
fi

# ── banner ────────────────────────────────────────────────────────────────────
echo ""
echo "obsidian-sync client setup"
echo "server: $REMOTE_URL"
echo ""

# ── inputs ────────────────────────────────────────────────────────────────────
read -rp "Obsidian folder [$DEFAULT_ROOT]: " ROOT_PATH
ROOT_PATH="${ROOT_PATH:-$DEFAULT_ROOT}"

if [[ ! -d "$ROOT_PATH" ]]; then
    read -rp "'$ROOT_PATH' doesn't exist — create it? [Y/n]: " _mk
    [[ "${_mk:-y}" =~ ^[Yy] ]] || die "aborted"
    mkdir -p "$ROOT_PATH"
    info "created: $ROOT_PATH"
fi

if [[ -z "${OBSIDIAN_SYNC_TOKEN:-}" ]]; then
    read -rsp "Sync token: " OBSIDIAN_SYNC_TOKEN; echo
fi
[[ -n "$OBSIDIAN_SYNC_TOKEN" ]] || die "token is required"

read -rp "Sync interval in seconds [30]: " INTERVAL
INTERVAL=${INTERVAL:-30}

HOOK_OBSIDIAN=false
if [[ "$OS" == "Linux" && -n "$OBSIDIAN_BIN" ]] || \
   [[ "$OS" == "Darwin" && -n "$OBSIDIAN_APP" ]]; then
    read -rp "Hook sync to Obsidian? Start/stop with the app [Y/n]: " _hook
    [[ "${_hook:-y}" =~ ^[Yy] ]] && HOOK_OBSIDIAN=true
fi

STATE_DB="$STATE_DIR/obsidian.db"

echo ""
info "root:     $ROOT_PATH"
info "remote:   $REMOTE_URL"
info "state:    $STATE_DB"
info "interval: ${INTERVAL}s"

# ── build ─────────────────────────────────────────────────────────────────────
BIN="$INSTALL_DIR/obsidian-sync-client"

header "build"

if [[ ! -f "$BIN" ]]; then
    command -v cmake >/dev/null || die "cmake not found — install it and re-run"
    command -v g++ >/dev/null || command -v c++ >/dev/null \
        || die "C++ compiler not found — install g++ or clang++ and re-run"
    BUILD_DIR="$(mktemp -d)"
    trap 'rm -rf "$BUILD_DIR"' EXIT
    info "building from source into $BUILD_DIR ..."
    cmake -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_POLICY_DEFAULT_CMP0135=NEW >/dev/null 2>&1 \
        || cmake -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
    cmake --build "$BUILD_DIR" --target obsidian-sync-client \
        -j"$(nproc 2>/dev/null || sysctl -n hw.logicalcpu)"
    mkdir -p "$INSTALL_DIR"
    install -m 0755 "$BUILD_DIR/obsidian-sync-client" "$BIN"
    info "installed $BIN"
else
    info "binary already at $BIN — skipping build (delete it to rebuild)"
fi

mkdir -p "$STATE_DIR"

# ── service setup ─────────────────────────────────────────────────────────────
header "service"

SERVICE_NAME="obsidian-sync"

if [[ "$OS" == "Linux" ]]; then
    SERVICE_DIR="$HOME/.config/systemd/user"
    SERVICE_FILE="$SERVICE_DIR/${SERVICE_NAME}.service"
    mkdir -p "$SERVICE_DIR"

    cat > "$SERVICE_FILE" <<EOF
[Unit]
Description=Obsidian Sync
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
Environment=OBSIDIAN_SYNC_TOKEN=${OBSIDIAN_SYNC_TOKEN}
ExecStart=${BIN} \\
    --local-root "${ROOT_PATH}" \\
    --remote-url ${REMOTE_URL} \\
    --state ${STATE_DB} \\
    --apply \\
    --watch \\
    --interval ${INTERVAL}
Restart=on-failure
RestartSec=10

[Install]
WantedBy=default.target
EOF

    systemctl --user daemon-reload
    if $HOOK_OBSIDIAN; then
        systemctl --user disable "${SERVICE_NAME}.service" 2>/dev/null || true
        systemctl --user stop    "${SERVICE_NAME}.service" 2>/dev/null || true
        info "service installed (Obsidian hook will manage it)"
    else
        systemctl --user enable --now "${SERVICE_NAME}.service"
        info "enabled: ${SERVICE_NAME}"
        info "logs:  journalctl --user -u ${SERVICE_NAME} -f"
        info "stop:  systemctl --user stop ${SERVICE_NAME}"
    fi

elif [[ "$OS" == "Darwin" ]]; then
    $HOOK_OBSIDIAN && _autostart="false" || _autostart="true"
    PLIST_LABEL="org.pemsoft.${SERVICE_NAME}"
    PLIST_FILE="$HOME/Library/LaunchAgents/${PLIST_LABEL}.plist"
    LOG_OUT="/tmp/${SERVICE_NAME}.out.log"
    LOG_ERR="/tmp/${SERVICE_NAME}.err.log"
    mkdir -p "$HOME/Library/LaunchAgents"

    cat > "$PLIST_FILE" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN"
  "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>Label</key>              <string>${PLIST_LABEL}</string>
  <key>ProgramArguments</key>
  <array>
    <string>${BIN}</string>
    <string>--local-root</string>  <string>${ROOT_PATH}</string>
    <string>--remote-url</string>  <string>${REMOTE_URL}</string>
    <string>--state</string>       <string>${STATE_DB}</string>
    <string>--apply</string>
    <string>--watch</string>
    <string>--interval</string>    <string>${INTERVAL}</string>
  </array>
  <key>EnvironmentVariables</key>
  <dict>
    <key>OBSIDIAN_SYNC_TOKEN</key> <string>${OBSIDIAN_SYNC_TOKEN}</string>
  </dict>
  <key>RunAtLoad</key>   <${_autostart}/>
  <key>KeepAlive</key>   <${_autostart}/>
  <key>StandardOutPath</key>  <string>${LOG_OUT}</string>
  <key>StandardErrorPath</key><string>${LOG_ERR}</string>
</dict>
</plist>
EOF

    launchctl unload "$PLIST_FILE" 2>/dev/null || true
    launchctl load   "$PLIST_FILE"
    info "LaunchAgent: $PLIST_LABEL"
    info "logs:  tail -f $LOG_OUT $LOG_ERR"
fi

# ── obsidian hook ─────────────────────────────────────────────────────────────
if $HOOK_OBSIDIAN; then
    header "obsidian hook"

    if [[ "$OS" == "Linux" ]]; then
        WRAPPER="$INSTALL_DIR/obsidian-synced"
        cat > "$WRAPPER" <<EOF
#!/usr/bin/env bash
systemctl --user start ${SERVICE_NAME}.service
${OBSIDIAN_BIN} "\$@"
systemctl --user stop ${SERVICE_NAME}.service
EOF
        chmod +x "$WRAPPER"

        DESKTOP_DIR="$HOME/.local/share/applications"
        mkdir -p "$DESKTOP_DIR"
        cat > "$DESKTOP_DIR/obsidian.desktop" <<EOF
[Desktop Entry]
Name=Obsidian
Exec=${WRAPPER} %U
Terminal=false
Type=Application
Icon=obsidian
StartupWMClass=obsidian
Comment=Obsidian
MimeType=x-scheme-handler/obsidian;
Categories=Office;
EOF
        update-desktop-database "$DESKTOP_DIR" 2>/dev/null || true
        info "wrapper:       $WRAPPER"
        info "desktop entry: updated — launch Obsidian normally to start sync"

    elif [[ "$OS" == "Darwin" ]]; then
        APP="$HOME/Applications/Obsidian Synced.app"
        APP_BIN="$APP/Contents/MacOS/obsidian-synced"
        mkdir -p "$APP/Contents/MacOS"

        cat > "$APP/Contents/Info.plist" <<'PEOF'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN"
  "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleExecutable</key>  <string>obsidian-synced</string>
  <key>CFBundleIdentifier</key>  <string>org.pemsoft.obsidian-synced</string>
  <key>CFBundleName</key>        <string>Obsidian Synced</string>
  <key>CFBundleVersion</key>     <string>1</string>
  <key>CFBundlePackageType</key> <string>APPL</string>
  <key>LSUIElement</key>         <false/>
</dict>
</plist>
PEOF
        cat > "$APP_BIN" <<EOF
#!/usr/bin/env bash
launchctl start ${PLIST_LABEL}
open -W "${OBSIDIAN_APP}"
launchctl stop ${PLIST_LABEL}
EOF
        chmod +x "$APP_BIN"
        info "app:  $APP"
        info "Open 'Obsidian Synced' from ~/Applications or Spotlight"
    fi
fi

# ── first sync preview ────────────────────────────────────────────────────────
header "first sync preview (dry run)"
echo ""
OBSIDIAN_SYNC_TOKEN="$OBSIDIAN_SYNC_TOKEN" \
    "$BIN" --local-root "$ROOT_PATH" --remote-url "$REMOTE_URL" --state "$STATE_DB" \
    2>&1 | sed 's/^/  /' | head -30 || true

echo ""
echo "Setup complete. Syncing '$ROOT_PATH' every ${INTERVAL}s."
echo "Token: $OBSIDIAN_SYNC_TOKEN"
