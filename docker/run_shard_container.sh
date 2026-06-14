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
# reverse order. S1 opt-in: set S1_PARTY_FRONTENDS='party1=47916,party2=47917'.
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

nats_endpoint() {
	local url="${EGS_DSS_NATS_URL:-${EGS_SHEET_NATS_URL:-${NATS_URL:-nats://nats:4222}}}"
	if [ "$url" = "disabled" ]; then
		return 1
	fi
	url="${url#nats://}"
	url="${url#*@}"
	url="${url%%/*}"
	NATS_HOST="${url%%:*}"
	if [[ "$url" == *:* ]]; then
		NATS_PORT="${url##*:}"
	else
		NATS_PORT=4222
	fi
	[ -n "$NATS_HOST" ]
}

json_escape() {
	local value=$1
	value=${value//\\/\\\\}
	value=${value//\"/\\\"}
	value=${value//$'\n'/\\n}
	value=${value//$'\r'/\\r}
	value=${value//$'\t'/\\t}
	printf '%s' "$value"
}

publish_gm_party_frontend() { # party_id addr
	local party_id=$1 addr=$2 party_json addr_json body len
	if ! nats_endpoint; then
		echo "[warn] NATS disabled; not publishing route for $party_id"
		return 0
	fi
	party_json=$(json_escape "$party_id")
	addr_json=$(json_escape "$addr")
	body="{\"command\":\"set_party_frontend\",\"payload\":{\"party_id\":\"$party_json\",\"addr\":\"$addr_json\"}}"
	len=${#body}
	if ! exec 3<>/dev/tcp/"$NATS_HOST"/"$NATS_PORT"; then
		echo "[warn] unable to publish route for $party_id: NATS $NATS_HOST:$NATS_PORT unavailable"
		return 0
	fi
	printf 'CONNECT {"verbose":false,"pedantic":false,"lang":"ryzom-shard-runner","version":"0.1"}\r\nPUB gm.set_party_frontend %d\r\n%s\r\nPING\r\n' "$len" "$body" >&3
	exec 3<&-
	exec 3>&-
	echo "[route] $party_id -> $addr"
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

start_party_frontends() {
	local spec="${S1_PARTY_FRONTENDS:-${PARTY_FRONTENDS:-}}"
	[ -n "$spec" ] || return 0

	local bind_host="${S1_BIND_HOST:-0.0.0.0}"
	local route_host="${S1_ROUTE_HOST:-nel-shard}"
	local runtime_dir="s1_frontends"
	mkdir -p "$runtime_dir"

	local IFS=',' entries entry party_id target port bind_addr route_addr safe cfgdir cfg logfile pid
	read -ra entries <<< "$spec"
	for entry in "${entries[@]}"; do
		entry="${entry//[[:space:]]/}"
		[ -n "$entry" ] || continue
		if [[ "$entry" != *=* ]]; then
			echo "[warn] ignoring malformed S1_PARTY_FRONTENDS entry '$entry' (expected party=port or party=host:port)"
			continue
		fi

		party_id="${entry%%=*}"
		target="${entry#*=}"
		if [[ "$target" == *:* ]]; then
			port="${target##*:}"
			route_addr="$target"
		else
			port="$target"
			route_addr="$route_host:$port"
		fi
		bind_addr="$bind_host:$port"

		safe=${party_id//[^A-Za-z0-9_.-]/_}
		cfgdir="$runtime_dir/$safe"
		mkdir -p "$cfgdir"
		cp "cfg_dev/frontend_service.cfg" "$cfgdir/frontend_service.cfg"
		cp "common.cfg" "$cfgdir/common.cfg"
		cfg="$cfgdir/frontend_service.cfg"
		sed -i '/^SId[[:space:]]*=/d' "$cfg"
		sed -i "s|^ListenAddress.*|ListenAddress = \"$bind_addr\";|" "$cfg"

		logfile="logs_dev/frontend_service_$safe.out"
		: > "$logfile"
		"./ryzom_frontend_service" -C"$cfgdir" --noBg "-B$NS_ADDR" > "$logfile" 2>&1 &
		pid=$!
		PIDS+=("$pid")
		wait_ready "frontend_service:$party_id" "$logfile" "$pid" 120 || exit 1
		publish_gm_party_frontend "$party_id" "$route_addr"
	done
}

start_nel ryzom_tick_service    tick_service          60 || exit 1
start_nel ryzom_mirror_service  mirror_service        60 || exit 1
start_nel ryzom_gpm_service     gpm_service           60 || exit 1

# EGS via its own runner (strips/reappends RYZOM_DEV_OVERRIDES, honors a
# pre-set EGS_SHEETS_DB). First run George-compiles ~9K forms (slow); the
# packed-sheet cache in this mounted dir is shared with native runs.
: > logs_dev/entities_game_service.out
if [ -n "${S1_PARTY_FRONTENDS:-${PARTY_FRONTENDS:-}}" ]; then
	EGS_SHEET_NATS_URL="${EGS_SHEET_NATS_URL:-${NATS_URL:-nats://nats:4222}}" ./run_egs_dev.sh "-B$NS_ADDR" > logs_dev/entities_game_service.out 2>&1 &
else
	./run_egs_dev.sh "-B$NS_ADDR" > logs_dev/entities_game_service.out 2>&1 &
fi
EGS_PID=$!
PIDS+=("$EGS_PID")
wait_ready entities_game_service logs_dev/entities_game_service.out "$EGS_PID" 600 || exit 1

start_nel ryzom_frontend_service frontend_service     120 || exit 1
start_party_frontends

touch logs_dev/.shard_ready
echo "[shard] up — frontend UDP 47851 (compose network), naming at $NS_ADDR"
if [ -n "${S1_PARTY_FRONTENDS:-${PARTY_FRONTENDS:-}}" ]; then
	echo "[shard] S1 party frontends: ${S1_PARTY_FRONTENDS:-${PARTY_FRONTENDS:-}}"
fi

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
