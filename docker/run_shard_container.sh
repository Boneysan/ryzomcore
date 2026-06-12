#!/bin/bash
# Container shard runner (Task 5.5.1) — one container for the whole C++ game
# tier: backup → tick → mirror → gpms → EGS → frontend.
#
# Why one container: the NeL mirror system is SHARED MEMORY — mirror_service
# and every mirror client (EGS, GPMS, FS) must share an IPC namespace, so the
# game services cannot be split into per-service containers.
#
# Expects:
#   - cwd = the host build dir (ryzomcore/build/ryzom/bin/Debug), bind-mounted
#     at its HOST path so the absolute data paths inside the cfgs stay valid
#   - naming_service running elsewhere (compose: nel-naming), addr in NS_ADDR
#   - EGS_SHEETS_DB / EGS_SHEET_NATS_URL set by compose (postgres/nats hosts)
#
# Mirrors run_shard_dev.sh (same dir) — keep the two in sync. Differences:
# naming is external (-B override), frontend binds 0.0.0.0 (cross-container),
# logs_dev/.shard_ready is the compose healthcheck marker, TERM tears down in
# reverse order.
set -u
NS_ADDR="${NS_ADDR:-nel-naming:50000}"

mkdir -p logs_dev
rm -f logs_dev/.shard_ready

PIDS=()

shutdown() {
	echo "[shard] stopping (reverse order)"
	for ((i = ${#PIDS[@]} - 1; i >= 0; i--)); do
		kill "${PIDS[$i]}" 2>/dev/null
		wait "${PIDS[$i]}" 2>/dev/null
	done
	exit 0
}
trap shutdown TERM INT

wait_ready() { # name logfile pid timeout_s
	local name=$1 out=$2 pid=$3 timeout=${4:-90} i
	for ((i = 0; i < timeout * 2; i++)); do
		if grep -q "Service ready" "$out" 2>/dev/null; then
			echo "[ok]   $name"
			return 0
		fi
		if ! kill -0 "$pid" 2>/dev/null; then
			echo "[FAIL] $name exited early — tail of $out:"
			tail -25 "$out"
			return 1
		fi
		sleep 0.5
	done
	echo "[FAIL] $name not ready after ${timeout}s — tail of $out:"
	tail -25 "$out"
	return 1
}

start_nel() { # binary cfgname [timeout_s]
	local bin=$1 cfg=$2 timeout=${3:-90}
	if [ -f "cfg_dev/$cfg.cfg" ]; then
		cp "cfg_dev/$cfg.cfg" "$cfg.cfg"
	fi
	if [ "$cfg" = "frontend_service" ]; then
		# cross-container: the go-proxy dials this container over the compose
		# network; loopback binding would be unreachable
		sed -i 's/^ListenAddress.*/ListenAddress = "0.0.0.0:47851";/' "$cfg.cfg"
	fi
	: > "logs_dev/$cfg.out"
	"./$bin" -C. --noBg "-B$NS_ADDR" > "logs_dev/$cfg.out" 2>&1 &
	local pid=$!
	PIDS+=("$pid")
	wait_ready "$cfg" "logs_dev/$cfg.out" "$pid" "$timeout"
}

start_nel ryzom_backup_service  backup_service        60 || exit 1
start_nel ryzom_tick_service    tick_service          60 || exit 1
start_nel ryzom_mirror_service  mirror_service        60 || exit 1
start_nel ryzom_gpm_service     gpm_service           60 || exit 1

# EGS via its own runner (strips/reappends RYZOM_DEV_OVERRIDES, honors a
# pre-set EGS_SHEETS_DB). First run George-compiles ~9K forms (slow); the
# packed-sheet cache in this mounted dir is shared with native runs.
: > logs_dev/entities_game_service.out
./run_egs_dev.sh "-B$NS_ADDR" > logs_dev/entities_game_service.out 2>&1 &
EGS_PID=$!
PIDS+=("$EGS_PID")
wait_ready entities_game_service logs_dev/entities_game_service.out "$EGS_PID" 600 || exit 1

start_nel ryzom_frontend_service frontend_service     120 || exit 1

touch logs_dev/.shard_ready
echo "[shard] up — frontend UDP 47851 (compose network), naming at $NS_ADDR"

# PID1: stay alive while children run; exit non-zero if any service dies.
while true; do
	for pid in "${PIDS[@]}"; do
		if ! kill -0 "$pid" 2>/dev/null; then
			echo "[shard] a service exited — going down"
			rm -f logs_dev/.shard_ready
			shutdown
		fi
	done
	sleep 5
done
