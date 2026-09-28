#!/usr/bin/env bash
#
# Does the firmware compute the RIGHT gradient, or merely a gradient?
#
#   run A (FW_WORKER=0): a forked Linux worker, x86-64, hardware double-backed
#                        floats, expf() from glibc's libm
#   run B (default):     Cortex-M3 under emulation, software floats from libgcc,
#                        fm_expf() from firmware/fastmath.c

set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT" || exit 1

PORT=${FW_PORT:-5555}
TOL=${TOL:-1e-5}
ELF=firmware/build/firmware.elf
QEMU_WIN="/mnt/c/Program Files/qemu/qemu-system-arm.exe"

LOG_A=$(mktemp)
LOG_B=$(mktemp)

if command -v qemu-system-arm >/dev/null 2>&1; then
    QEMU=(qemu-system-arm)
    KERNEL_ARG="$ELF"
    KILL_QEMU() { pkill -9 -f 'qemu-system-arm.*lm3s6965evb' 2>/dev/null; }
    DEFAULT_HOST=127.0.0.1
elif [ -x "$QEMU_WIN" ]; then
    QEMU=("$QEMU_WIN")
    KERNEL_ARG=$(wslpath -w "$ROOT/$ELF" 2>/dev/null) || {
        echo "SKIP: wslpath unavailable"; exit 0; }
    KILL_QEMU() { taskkill.exe /F /IM qemu-system-arm.exe >/dev/null 2>&1; }
    DEFAULT_HOST=$(ip route show default | awk '{print $3}')
else
    echo "SKIP: no qemu-system-arm found"
    exit 0
fi

export FW_HOST=${FW_HOST:-$DEFAULT_HOST}
export FW_PORT=$PORT

[ -f "$ELF" ]          || { echo "SKIP: $ELF not built (make -C firmware)"; exit 0; }
[ -f bin/microkernel ] || { echo "SKIP: bin/microkernel not built (make)"; exit 0; }
[ -f shard_4.txt ]     || { echo "SKIP: shard_4.txt missing (python3 gen_shards.py)"; exit 0; }

cleanup() { KILL_QEMU; rm -f "$LOG_A" "$LOG_B"; }
trap cleanup EXIT

# Run the kernel to completion, nudging the monitor to quit once the rounds are
# done. (The monitor blocks on its command FIFO and never reads OP_TERMINATE, so
# without this the kernel waits on it forever -- pre-existing behaviour, present
# with or without the firmware worker.)
run_kernel() {
    local out=$1
    shift
    env "$@" ./bin/microkernel >"$out" 2>&1 &
    local pid=$!
    local waited=0 sent=0
    while kill -0 "$pid" 2>/dev/null && [ "$waited" -lt 90 ]; do
        if [ "$sent" -eq 0 ] && grep -q 'complete, alive_workers' "$out" 2>/dev/null; then
            echo 5 >/tmp/monitor_fifo 2>/dev/null
            sent=1
        fi
        sleep 1
        waited=$((waited + 1))
    done
    kill -9 "$pid" 2>/dev/null
    wait "$pid" 2>/dev/null
}

echo "=== firmware gradient equivalence test ==="
echo

echo ">>> run A: shard 4 on a forked Linux worker (glibc libm, hardware float)"
KILL_QEMU
run_kernel "$LOG_A" FW_WORKER=0

echo ">>> run B: shard 4 on the Cortex-M3 firmware (fastmath.c, software float)"
KILL_QEMU
sleep 1
"${QEMU[@]}" -M lm3s6965evb -kernel "$KERNEL_ARG" \
    -serial "tcp::$PORT,server" -serial null -display none >/dev/null 2>&1 &
sleep 2
run_kernel "$LOG_B" FW_WORKER=1
KILL_QEMU

# grad_norm is the parameter server's RMS of the averaged gradient across all
# four workers, so it depends on every contribution -- a wrong gradient from the
# firmware cannot hide in it.
extract() { grep -oP 'round \d+: grad_norm=\K[0-9.]+' "$1"; }

mapfile -t A < <(extract "$LOG_A")
mapfile -t B < <(extract "$LOG_B")

echo
echo "round |  linux worker  |  firmware      |  abs diff"
echo "------+----------------+----------------+-----------"

FAILED=0
if [ "${#A[@]}" -eq 0 ] || [ "${#B[@]}" -eq 0 ]; then
    echo "FAIL: no grad_norm lines captured (A=${#A[@]} B=${#B[@]})"
    echo "--- run A tail ---"; tail -15 "$LOG_A"
    echo "--- run B tail ---"; tail -15 "$LOG_B"
    exit 1
fi

if [ "${#A[@]}" -ne "${#B[@]}" ]; then
    echo "FAIL: different round counts (A=${#A[@]} B=${#B[@]}) -- a worker died mid-run"
    FAILED=1
fi

n=${#A[@]}
[ "${#B[@]}" -lt "$n" ] && n=${#B[@]}

for ((i = 0; i < n; i++)); do
    d=$(awk -v a="${A[$i]}" -v b="${B[$i]}" 'BEGIN{d=a-b; if(d<0)d=-d; printf "%.9f", d}')
    ok=$(awk -v d="$d" -v t="$TOL" 'BEGIN{print (d<=t)?"ok":"DIFF"}')
    printf "  %2d  |  %-12s  |  %-12s  |  %s  %s\n" "$i" "${A[$i]}" "${B[$i]}" "$d" \
        "$([ "$ok" = ok ] && echo "" || echo "<-- $ok")"
    [ "$ok" = ok ] || FAILED=1
done

echo
if [ "$FAILED" -eq 0 ]; then
    echo "PASS: the Cortex-M3 firmware and a Linux worker produce the same gradient"
    echo "      on the same shard, within $TOL."
    exit 0
fi
echo "FAIL: gradients diverge beyond $TOL -- the firmware's maths or its"
echo "      interpretation of the wire format is wrong."
exit 1
