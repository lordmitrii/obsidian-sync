#!/usr/bin/env bash
set -euo pipefail

URL="${1:-http://127.0.0.1:38471/healthz}"
SERVICE="${2:-obsidian-sync-server.service}"

if curl -sf -o /dev/null --max-time 5 "$URL"; then
    exit 0
fi

logger -t obsidian-sync-healthcheck "healthz check failed for $URL, restarting $SERVICE"
systemctl restart "$SERVICE"
