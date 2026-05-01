#!/usr/bin/env bash
# test-env.sh — local BitTorrent test harness: chihaya + transmission-cli seeder
set -euo pipefail

# --- config ---------------------------------------------------------------
TRACKER_IP="127.0.0.1"
SEEDER_IP="127.0.0.1"
TRACKER_HTTP_PORT=6969
TRACKER_UDP_PORT=6969
SEEDER_PORT=51413
WORKDIR="${WORKDIR:-$(pwd)/.bt-testenv}"
FILE_SIZE_MB="${FILE_SIZE_MB:-10}"

# --- deps -----------------------------------------------------------------
for bin in chihaya transmission-create transmission-cli; do
  if ! command -v "$bin" >/dev/null 2>&1; then
    echo "missing: $bin" >&2
    echo "install with:" >&2
    echo "  brew install transmission-cli" >&2
    echo "  go install github.com/chihaya/chihaya/cmd/chihaya@latest" >&2
    exit 1
  fi
done

mkdir -p "$WORKDIR"/{seed,logs}
cd "$WORKDIR"

# --- chihaya config -------------------------------------------------------
CHIHAYA_CFG="$WORKDIR/chihaya.yaml"
cat >"$CHIHAYA_CFG" <<EOF
chihaya:
  announce_interval: 30s
  min_announce_interval: 15s
  metrics_addr: "0.0.0.0:6880"

  http:
    addr: "0.0.0.0:${TRACKER_HTTP_PORT}"
    read_timeout: 5s
    write_timeout: 5s
    allow_ip_spoofing: true
    real_ip_header: ""
    max_numwant: 100
    default_numwant: 50
    max_scrape_infohashes: 50
    announce_routes:
      - "/announce"
    scrape_routes:
      - "/scrape"

  udp:
    addr: "0.0.0.0:${TRACKER_UDP_PORT}"
    max_clock_skew: 10s
    private_key: "swirrent-test-env-private-key-change-me"
    allow_ip_spoofing: true
    max_numwant: 100
    default_numwant: 50
    max_scrape_infohashes: 50

  storage:
    name: memory
    config:
      gc_interval: 3m
      peer_lifetime: 31m
      shard_count: 1024
      prometheus_reporting_interval: 1s
EOF

# --- test payload + torrent ----------------------------------------------
if [[ ! -f seed/test.bin ]]; then
  echo "[+] creating ${FILE_SIZE_MB}MiB random payload"
  dd if=/dev/urandom of=seed/test.bin bs=1M count="$FILE_SIZE_MB" status=none
fi

TORRENT="$WORKDIR/test.torrent"
if [[ ! -f "$TORRENT" ]]; then
  echo "[+] creating torrent"
  transmission-create \
    -o "$TORRENT" \
    -t "http://$TRACKER_IP:${TRACKER_HTTP_PORT}/announce" \
    -t "udp://$TRACKER_IP:${TRACKER_UDP_PORT}/announce" \
    seed/test.bin >/dev/null
fi

# --- cleanup --------------------------------------------------------------
pids=()
cleanup() {
  echo
  echo "[+] shutting down"
  for pid in "${pids[@]}"; do
    kill "$pid" 2>/dev/null || true
  done
  wait 2>/dev/null || true
}
trap cleanup EXIT INT TERM

# --- tracker --------------------------------------------------------------
echo "[+] starting chihaya (http :${TRACKER_HTTP_PORT}, udp :${TRACKER_UDP_PORT})"
chihaya --config "$CHIHAYA_CFG" \
  >"$WORKDIR/logs/chihaya.log" 2>&1 &
pids+=("$!")

sleep 0.5

# --- seeder ---------------------------------------------------------------
echo "[+] starting transmission-cli seeder on :${SEEDER_PORT}"
transmission-cli \
  -p "$SEEDER_PORT" \
  -w "$WORKDIR/seed" \
  -f "echo seeder: torrent finished verifying" \
  "$TORRENT" \
  >"$WORKDIR/logs/seeder.log" 2>&1 &
pids+=("$!")

# --- info -----------------------------------------------------------------
cat <<EOF

===========================================================
  Local BitTorrent test environment running
-----------------------------------------------------------
  tracker     : http://$TRACKER_IP:${TRACKER_HTTP_PORT}/announce
                udp://$TRACKER_IP:${TRACKER_UDP_PORT}/announce
  metrics     : http://$TRACKER_IP:6880/metrics
  seeder peer : $SEEDER_IP:${SEEDER_PORT}
  torrent     : ${TORRENT}
  payload     : ${WORKDIR}/seed/test.bin
  logs        : ${WORKDIR}/logs/
-----------------------------------------------------------
  try:
    ./swirrent ${TORRENT} -v
    ./swirrent e.torrent --handshake $SEEDER_IP:${SEEDER_PORT}

  Ctrl-C to stop.
===========================================================
EOF

wait
