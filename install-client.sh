#!/usr/bin/env bash
set -euo pipefail


REMOTE_URL="${OBSIDIAN_SYNC_REMOTE_URL:-https://sync.utils.pemsoft.org}"
INSTALL_DIR="$HOME/.local/bin"
STATE_DIR="$HOME/.local/share/obsidian-sync"
OS=$(uname -s)


die()  { echo "error: $*" >&2; exit 1; }
info() { echo "  $*"; }
ask()  { local v; read -rp "$1: " v; echo "$v"; }


echo ""
echo "obsidian-sync client setup"
echo "server: $REMOTE_URL"
echo ""


VAULT_PATH=$(ask "Vault path (absolute)")
[[ -d "$VAULT_PATH" ]] || die "directory not found: $VAULT_PATH"

if [[ -z "${OBSIDIAN_SYNC_TOKEN:-}" ]]; then
    read -rsp "Sync token: " OBSIDIAN_SYNC_TOKEN; echo
fi
[[ -n "$OBSIDIAN_SYNC_TOKEN" ]] || die "token is required"

INTERVAL=$(ask "Sync interval in seconds [30]")
INTERVAL=${INTERVAL:-30}

HOOK_OBSIDIAN=false
if [[ "$OS" == "Linux" ]] && command -v obsidian >/dev/null 2>&1; then
    read -rp "Hook sync to Obsidian? Start/stop with the app [Y/n]: " _hook
    [[ "${_hook:-y}" =~ ^[Yy] ]] && HOOK_OBSIDIAN=true
elif [[ "$OS" == "Darwin" ]]; then
    OBSIDIAN_APP=$(mdfind "kMDItemCFBundleIdentifier == 'md.obsidian'" 2>/dev/null | head -1)
    if [[ -n "$OBSIDIAN_APP" ]]; then
        read -rp "Hook sync to Obsidian? Start/stop with the app [Y/n]: " _hook
        [[ "${_hook:-y}" =~ ^[Yy] ]] && HOOK_OBSIDIAN=true
    fi
fi

VAULT_NAME=$(basename "$VAULT_PATH")
SLUG=$(echo "$VAULT_NAME" | tr '[:upper:]' '[:lower:]' | tr -cs 'a-z0-9' '-' | sed 's/-$//')
STATE_DB="$STATE_DIR/$SLUG.db"

echo ""
info "vault:    $VAULT_PATH"
info "slug:     $SLUG"
info "state db: $STATE_DB"
info "interval: ${INTERVAL}s"
echo ""


BIN="$INSTALL_DIR/obsidian-sync-client"

echo "── build ──────────────────────────────────────────────────────────────────"

if [[ ! -f "$BIN" ]]; then
    command -v cmake >/dev/null || die "cmake not found — install it and re-run"
    command -v g++   >/dev/null || command -v c++ >/dev/null \
        || die "C++ compiler not found — install g++ or clang++ and re-run"

    BUILD_DIR="$(mktemp -d)"
    trap 'rm -rf "$BUILD_DIR"' EXIT

    info "building from source into $BUILD_DIR ..."
    cmake -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_DEFAULT_CMP0135=NEW \
        >/dev/null 2>&1 || cmake -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
    cmake --build "$BUILD_DIR" --target obsidian-sync-client -j"$(nproc 2>/dev/null || sysctl -n hw.logicalcpu)"
    mkdir -p "$INSTALL_DIR"
    install -m 0755 "$BUILD_DIR/obsidian-sync-client" "$BIN"
    info "installed $BIN"
else
    info "binary already at $BIN — skipping build (delete it to rebuild)"
fi


mkdir -p "$STATE_DIR"


echo ""
echo "── service ────────────────────────────────────────────────────────────────"

if [[ "$OS" == "Linux" ]]; then
    SERVICE_DIR="$HOME/.config/systemd/user"
    SERVICE_FILE="$SERVICE_DIR/obsidian-sync-${SLUG}.service"
    mkdir -p "$SERVICE_DIR"

    cat > "$SERVICE_FILE" <<EOF
[Unit]
Description=Obsidian Sync - ${VAULT_NAME}
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
Environment=OBSIDIAN_SYNC_TOKEN=${OBSIDIAN_SYNC_TOKEN}
ExecStart=${BIN} \\
    --local-root "${VAULT_PATH}" \\
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
        systemctl --user disable "obsidian-sync-${SLUG}.service" 2>/dev/null || true
        systemctl --user stop    "obsidian-sync-${SLUG}.service" 2>/dev/null || true
        info "service installed (not auto-started — Obsidian will manage it)"
    else
        systemctl --user enable --now "obsidian-sync-${SLUG}.service"
        info "service enabled: obsidian-sync-${SLUG}"
        info "logs:  journalctl --user -u obsidian-sync-${SLUG} -f"
        info "stop:  systemctl --user stop obsidian-sync-${SLUG}"
    fi

elif [[ "$OS" == "Darwin" ]]; then
    $HOOK_OBSIDIAN && _plist_autostart="false" || _plist_autostart="true"
    PLIST_DIR="$HOME/Library/LaunchAgents"
    PLIST_LABEL="org.pemsoft.obsidian-sync.${SLUG}"
    PLIST_FILE="$PLIST_DIR/${PLIST_LABEL}.plist"
    LOG_OUT="/tmp/obsidian-sync-${SLUG}.out.log"
    LOG_ERR="/tmp/obsidian-sync-${SLUG}.err.log"
    mkdir -p "$PLIST_DIR"

    cat > "$PLIST_FILE" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN"
  "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>Label</key>
  <string>${PLIST_LABEL}</string>

  <key>ProgramArguments</key>
  <array>
    <string>${BIN}</string>
    <string>--local-root</string>
    <string>${VAULT_PATH}</string>
    <string>--remote-url</string>
    <string>${REMOTE_URL}</string>
    <string>--state</string>
    <string>${STATE_DB}</string>
    <string>--apply</string>
    <string>--watch</string>
    <string>--interval</string>
    <string>${INTERVAL}</string>
  </array>

  <key>EnvironmentVariables</key>
  <dict>
    <key>OBSIDIAN_SYNC_TOKEN</key>
    <string>${OBSIDIAN_SYNC_TOKEN}</string>
  </dict>

  <key>RunAtLoad</key>
  <${_plist_autostart}/>
  <key>KeepAlive</key>
  <${_plist_autostart}/>

  <key>StandardOutPath</key>
  <string>${LOG_OUT}</string>
  <key>StandardErrorPath</key>
  <string>${LOG_ERR}</string>
</dict>
</plist>
EOF

    launchctl unload "$PLIST_FILE" 2>/dev/null || true
    launchctl load   "$PLIST_FILE"
    info "LaunchAgent loaded: $PLIST_LABEL"
    info "logs:  tail -f $LOG_OUT $LOG_ERR"
    info "stop:  launchctl unload $PLIST_FILE"

else
    die "unsupported OS: $OS (only Linux and macOS are supported)"
fi



# ── obsidian hook ─────────────────────────────────────────────────────────────

if $HOOK_OBSIDIAN; then
    echo ""
    echo "── obsidian hook ──────────────────────────────────────────────────────────"

    if [[ "$OS" == "Linux" ]]; then
        OBSIDIAN_BIN=$(command -v obsidian)
        WRAPPER="$INSTALL_DIR/obsidian-synced"
        DESKTOP_DIR="$HOME/.local/share/applications"

        cat > "$WRAPPER" <<EOF
#!/usr/bin/env bash
SERVICE="obsidian-sync-${SLUG}.service"
systemctl --user start "\$SERVICE"
${OBSIDIAN_BIN} "\$@"
systemctl --user stop "\$SERVICE"
EOF
        chmod +x "$WRAPPER"

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
        info "desktop entry: $DESKTOP_DIR/obsidian.desktop"

    elif [[ "$OS" == "Darwin" ]]; then
        PLIST_LABEL="org.pemsoft.obsidian-sync.${SLUG}"
        APP_DIR="$HOME/Applications"
        APP="$APP_DIR/Obsidian Synced.app"
        APP_BIN="$APP/Contents/MacOS/obsidian-synced"
        mkdir -p "$APP/Contents/MacOS"

        # Minimal Info.plist so macOS recognises it as a real app
        cat > "$APP/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN"
  "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleExecutable</key>     <string>obsidian-synced</string>
  <key>CFBundleIdentifier</key>     <string>org.pemsoft.obsidian-synced</string>
  <key>CFBundleName</key>           <string>Obsidian Synced</string>
  <key>CFBundleVersion</key>        <string>1</string>
  <key>CFBundlePackageType</key>    <string>APPL</string>
  <key>LSUIElement</key>            <false/>
</dict>
</plist>
EOF

        cat > "$APP_BIN" <<EOF
#!/usr/bin/env bash
# Start sync, open Obsidian, stop sync when Obsidian quits
launchctl start "${PLIST_LABEL}"
open -W "${OBSIDIAN_APP}"
launchctl stop "${PLIST_LABEL}"
EOF
        chmod +x "$APP_BIN"

        info "app: $APP"
        info "Open 'Obsidian Synced' from ~/Applications or Spotlight instead of Obsidian"
        info "sync runs only while Obsidian is open"
    fi
fi

echo ""
echo "── first sync preview ─────────────────────────────────────────────────────"
OBSIDIAN_SYNC_TOKEN="$OBSIDIAN_SYNC_TOKEN" \
    "$BIN" --local-root "$VAULT_PATH" --remote-url "$REMOTE_URL" --state "$STATE_DB" \
    2>&1 | head -30 || true

echo ""
echo "Setup complete. The service will sync every ${INTERVAL}s."
echo "Token (keep this safe): $OBSIDIAN_SYNC_TOKEN"
