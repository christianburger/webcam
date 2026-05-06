#!/usr/bin/env bash
set -euo pipefail

# Watches an mDNS name, keeps cloudflared ingress upstream in sync,
# and restarts cloudflared when upstream changes or health checks fail.

MDNS_NAME="${MDNS_NAME:-web-cam.local}"
PORT="${PORT:-80}"
HOSTNAME_MAIN="${HOSTNAME_MAIN:-cam.runtracer.com}"
HOSTNAME_STATUS="${HOSTNAME_STATUS:-}"
HOSTNAME_STREAM="${HOSTNAME_STREAM:-}"
TUNNEL_UUID="${TUNNEL_UUID:-}"
CREDENTIALS_FILE="${CREDENTIALS_FILE:-$HOME/.cloudflared/${TUNNEL_UUID}.json}"
CONFIG_PATH="${CONFIG_PATH:-$HOME/.cloudflared/config.yml}"
CHECK_INTERVAL_SEC="${CHECK_INTERVAL_SEC:-15}"
STATE_FILE="${STATE_FILE:-/tmp/cloudflared-mdns-watch.last_ip}"
CLOUDFLARED_SERVICE="${CLOUDFLARED_SERVICE:-cloudflared}"

if [[ -z "${TUNNEL_UUID}" ]]; then
  echo "ERROR: TUNNEL_UUID is required" >&2
  exit 1
fi

assert_locally_managed_tunnel() {
  # This script edits local config.yml ingress. It cannot control remotely-managed
  # token tunnels configured only from the Cloudflare dashboard.
  if systemctl cat "$CLOUDFLARED_SERVICE" >/dev/null 2>&1; then
    if systemctl cat "$CLOUDFLARED_SERVICE" | grep -q -- "--token"; then
      echo "ERROR: ${CLOUDFLARED_SERVICE} is running with --token (remotely managed tunnel)." >&2
      echo "ERROR: Local sed edits to ${CONFIG_PATH} will not change dashboard ingress." >&2
      echo "ERROR: Use a locally-managed tunnel (config.yml) or update origin via Cloudflare API." >&2
      exit 2
    fi
  fi
}

resolve_ip() {
  local ip=""
  if command -v getent >/dev/null 2>&1; then
    ip="$(getent hosts "$MDNS_NAME" | awk '{print $1; exit}' || true)"
  fi

  if [[ -z "$ip" ]] && command -v avahi-resolve-host-name >/dev/null 2>&1; then
    ip="$(avahi-resolve-host-name "$MDNS_NAME" 2>/dev/null | awk '{print $2; exit}' || true)"
  fi

  if [[ -z "$ip" ]]; then
    return 1
  fi

  printf '%s\n' "$ip"
}

ensure_config_exists() {
  if [[ -f "$CONFIG_PATH" ]]; then
    return 0
  fi

  mkdir -p "$(dirname "$CONFIG_PATH")"
  cat > "$CONFIG_PATH" <<YAML
tunnel: ${TUNNEL_UUID}
credentials-file: ${CREDENTIALS_FILE}

ingress:
  - hostname: ${HOSTNAME_MAIN}
    service: http://127.0.0.1:${PORT}
$( [[ -n "${HOSTNAME_STATUS}" ]] && printf "  - hostname: %s\n    service: http://127.0.0.1:%s\n" "${HOSTNAME_STATUS}" "${PORT}" )
$( [[ -n "${HOSTNAME_STREAM}" ]] && printf "  - hostname: %s\n    service: http://127.0.0.1:%s\n" "${HOSTNAME_STREAM}" "${PORT}" )
  - service: http_status:404
YAML
}

update_config_ip_with_sed() {
  local ip="$1"
  ensure_config_exists

  # Update service URL following each configured hostname entry.
  sed -i \
    -e "/hostname: ${HOSTNAME_MAIN//\//\\/}/{n;s|^[[:space:]]*service:.*|    service: http://${ip}:${PORT}|;}" \
    "$CONFIG_PATH"

  if [[ -n "$HOSTNAME_STATUS" ]]; then
    sed -i -e "/hostname: ${HOSTNAME_STATUS//\//\\/}/{n;s|^[[:space:]]*service:.*|    service: http://${ip}:${PORT}|;}" "$CONFIG_PATH"
  fi
  if [[ -n "$HOSTNAME_STREAM" ]]; then
    sed -i -e "/hostname: ${HOSTNAME_STREAM//\//\\/}/{n;s|^[[:space:]]*service:.*|    service: http://${ip}:${PORT}|;}" "$CONFIG_PATH"
  fi
}

restart_cloudflared() {
  if command -v systemctl >/dev/null 2>&1; then
    systemctl restart "$CLOUDFLARED_SERVICE"
  else
    pkill -f "cloudflared tunnel" || true
    nohup cloudflared tunnel run >/tmp/cloudflared.out 2>&1 &
  fi
}

healthcheck() {
  local ip="$1"
  curl -fsS --max-time 3 "http://${ip}:${PORT}/status" >/dev/null
}

last_ip=""
[[ -f "$STATE_FILE" ]] && last_ip="$(cat "$STATE_FILE" 2>/dev/null || true)"
assert_locally_managed_tunnel

while true; do
  current_ip="$(resolve_ip || true)"

  if [[ -z "$current_ip" ]]; then
    echo "WARN: could not resolve ${MDNS_NAME}; keeping current config"
    sleep "$CHECK_INTERVAL_SEC"
    continue
  fi

  changed=0
  if [[ "$current_ip" != "$last_ip" ]]; then
    echo "INFO: IP changed ${last_ip:-<none>} -> ${current_ip}"
    changed=1
  fi

  if ! healthcheck "$current_ip"; then
    echo "WARN: healthcheck failed for ${current_ip}, forcing config update + restart"
    changed=1
  fi

  if [[ "$changed" -eq 1 ]]; then
    update_config_ip_with_sed "$current_ip"
    echo "$current_ip" > "$STATE_FILE"
    restart_cloudflared
    last_ip="$current_ip"
  fi

  sleep "$CHECK_INTERVAL_SEC"
done
