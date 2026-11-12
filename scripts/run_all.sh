#!/usr/bin/env bash
# Build, start hub (+ monitor / dashboard), clean up IPC objects on exit.
set -eo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SAN=""; GUI=0; MONITOR=1

usage() {
    cat <<EOF
Usage: scripts/run_all.sh [options]
  --gui          also start the Qt dashboard (needs a desktop session)
  --no-monitor   do not start the terminal monitor (hub runs until Ctrl+C)
  --tsan         build + run with ThreadSanitizer   (build-tsan/)
  --asan         build + run with ASan + UBSan      (build-asan/)
  -h, --help     show this help
EOF
}

for a in "$@"; do
    case "$a" in
        --gui)        GUI=1 ;;
        --no-monitor) MONITOR=0 ;;
        --tsan)       SAN=thread ;;
        --asan)       SAN=address ;;
        -h|--help)    usage; exit 0 ;;
        *)            echo "Unknown option: $a"; usage; exit 2 ;;
    esac
done

case "$SAN" in
    thread)  BUILD="$ROOT/build-tsan"
             echo "Note: if TSan fails with 'unexpected memory mapping', run: sudo sysctl vm.mmap_rnd_bits=28" ;;
    address) BUILD="$ROOT/build-asan" ;;
    *)       BUILD="$ROOT/build" ;;
esac

if pgrep -x sensor_hub >/dev/null; then
    echo "sensor_hub is already running (pkill -x sensor_hub to stop it)"; exit 1
fi

echo "==> Building in $BUILD"
cmake -S "$ROOT" -B "$BUILD" -DSANITIZE="$SAN" >/dev/null
cmake --build "$BUILD" -j"$(nproc)"

PIDS=()
cleanup() {
    trap - EXIT
    if ((${#PIDS[@]})); then
        for pid in "${PIDS[@]}"; do kill -TERM "$pid" 2>/dev/null || true; done
        for _ in $(seq 1 50); do                      # chờ tối đa 5 giây
            alive=0
            for pid in "${PIDS[@]}"; do kill -0 "$pid" 2>/dev/null && alive=1; done
            ((alive)) || break
            sleep 0.1
        done
        for pid in "${PIDS[@]}"; do kill -KILL "$pid" 2>/dev/null || true; done
    fi
    # Dọn rác IPC (trường hợp hub bị kill -9)
    rm -f /dev/shm/sensor_hub_shm /dev/mqueue/sensor_hub_alarm
    echo "Cleaned up."
}
trap 'exit 130' INT TERM
trap cleanup EXIT

echo "==> Starting hub (log: $ROOT/sensor_hub.log)"
(cd "$ROOT" && exec "$BUILD/sensor_hub") &
HUB_PID=$!
PIDS+=("$HUB_PID")

for _ in $(seq 1 50); do
    [ -e /dev/shm/sensor_hub_shm ] && break
    kill -0 "$HUB_PID" 2>/dev/null || { echo "Hub exited during startup, see sensor_hub.log"; exit 1; }
    sleep 0.1
done

DASH="$BUILD/dashboard/dashboard"
if ((GUI)); then
    if [ -x "$DASH" ] && { [ -n "$DISPLAY" ] || [ -n "$WAYLAND_DISPLAY" ]; }; then
        "$DASH" &
        PIDS+=("$!")
    else
        echo "Dashboard unavailable (not built, or no desktop session)."
    fi
fi

if ((MONITOR)); then
    "$BUILD/monitor" || true
else
    echo "Hub running (pid $HUB_PID). Press Ctrl+C to stop."
    wait "$HUB_PID" || true
fi
