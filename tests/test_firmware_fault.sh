#!/usr/bin/env bash
#
# Tier 3 verification: the firmware worker is indistinguishable from a forked
# worker, including how its death is detected.
#
# Sequence:
#   1. start QEMU with the firmware, UART0 on a TCP socket
#   2. start the kernel; it connects and runs training with 4 workers
#   3. mid-run, SIGKILL QEMU -- the abrupt kind of death, not a clean shutdown
#   4. assert the kernel notices, drops to 3 workers, and finishes anyway
#
# Rounds are slowed with SPECTATE so there is a window to kill QEMU inside.

set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT" || exit 1

PORT=${FW_PORT:-5555}
KILL_AFTER=${KILL_AFTER:-8}
ROUND_DELAY_MS=${ROUND_DELAY_MS:-1500}
LOG=$(mktemp)
QEMU_LOG=$(mktemp)

ELF=firmware/build/firmware.elf

fail() { echo "FAIL: $*"; FAILED=1; }
FAILED=0

# ---- locate QEMU ------------------------------------------------------------
# Prefer a native Linux qemu; fall back to the Windows build reached through
# /mnt/c, which is how this project is set up on the author's machine. The
# Windows binary cannot read /mnt/... paths, so the ELF has to be translated.
QEMU_WIN="/mnt/c/Program Files/qemu/qemu-system-arm.exe"

if command -v qemu-system-arm >/dev/null 2>&1; then
    QEMU=(qemu-system-arm)
    KERNEL_ARG="$ELF"
    KILL_QEMU() { [ -n "${QEMU_PID:-}" ] && kill -9 "$QEMU_PID" 2>/dev/null; }
elif [ -x "$QEMU_WIN" ]; then
    QEMU=("$QEMU_WIN")
    KERNEL_ARG=$(wslpath -w "$ROOT/$ELF")
    # Killing the WSL-side child does not reliably kill the Windows process it
    # launched, so reach for the Windows tool instead. /F is SIGKILL's cousin:
    # no chance for QEMU to close the socket politely, which is the point --
    # we want an abrupt death, exactly like a crash.
    KILL_QEMU() { taskkill.exe /F /IM qemu-system-arm.exe >/dev/null 2>&1; }
else
    echo "SKIP: no qemu-system-arm found (native or at $QEMU_WIN)"
    exit 0
fi

# ---- where is QEMU listening, from our point of view? -----------------------
# If QEMU is the Windows binary and we are inside WSL2, 'localhost' is the WSL
# VM, not the host. The host is the default gateway, and its address changes
# whenever WSL restarts -- so it is resolved at run time, never hardcoded.
if [ -n "${FW_HOST:-}" ]; then
    HOST=$FW_HOST
elif [ "${QEMU[0]}" = "$QEMU_WIN" ]; then
    HOST=$(ip route show default | awk '{print $3}')
else
    HOST=127.0.0.1
fi
export FW_HOST=$HOST
export FW_PORT=$PORT

# ---- preconditions ----------------------------------------------------------
[ -f "$ELF" ]        || { echo "SKIP: $ELF not built (run: make -C firmware)"; exit 0; }
[ -f bin/microkernel ] || { echo "SKIP: bin/microkernel not built (run: make)"; exit 0; }
[ -f shard_1.txt ]   || { echo "SKIP: shards not generated (run: python3 gen_shards.py)"; exit 0; }

cleanup() {
    KILL_QEMU
    [ -n "${KPID:-}" ] && kill -9 "$KPID" 2>/dev/null
    rm -f "$LOG" "$QEMU_LOG"
}
trap cleanup EXIT

echo "=== firmware fault-detection test ==="
echo "    qemu   : ${QEMU[0]}"
echo "    connect: $HOST:$PORT"
echo "    kill at: ${KILL_AFTER}s into the run"
echo

KILL_QEMU            # clear any stragglers from a previous run
sleep 1

# No ',nowait': QEMU holds the guest in reset until a client connects, so the
# firmware cannot boot and emit its OP_REGISTER into a void.
"${QEMU[@]}" -M lm3s6965evb -kernel "$KERNEL_ARG" \
    -serial "tcp::$PORT,server" \
    -serial null \
    -display none >"$QEMU_LOG" 2>&1 &
QEMU_PID=$!
sleep 2

SPECTATE=$ROUND_DELAY_MS ./bin/microkernel >"$LOG" 2>&1 &
KPID=$!

sleep "$KILL_AFTER"
echo ">>> killing QEMU (simulating firmware crash)"
KILL_QEMU

# Give the kernel time to hit its next round and notice. If it hangs instead,
# that is itself the failure this test exists to catch.
WAITED=0
QUIT_SENT=0
while kill -0 "$KPID" 2>/dev/null && [ "$WAITED" -lt 60 ]; do
    # Once the rounds are done, tell the monitor to quit.
    #
    # This is not part of the fault-detection test -- it works around existing
    # behaviour of the monitor. shutdown_all() waitpid()s every child, but the
    # monitor blocks reading its command FIFO and never reads the kernel's
    # OP_TERMINATE from stdin, so it never exits and the kernel waits forever.
    # That happens identically with FW_WORKER=0 and no QEMU present, so it is
    # unrelated to the firmware worker; sending '5' is what a human operator
    # would do at this point.
    if [ "$QUIT_SENT" -eq 0 ] && grep -q 'complete, alive_workers' "$LOG" 2>/dev/null; then
        echo ">>> rounds finished; sending quit to the monitor FIFO"
        echo 5 >/tmp/monitor_fifo 2>/dev/null
        QUIT_SENT=1
    fi
    sleep 1
    WAITED=$((WAITED + 1))
done

if kill -0 "$KPID" 2>/dev/null; then
    fail "kernel still running 60s after QEMU died -- it hung instead of detecting the fault"
    kill -9 "$KPID" 2>/dev/null
fi

echo
echo "--- kernel round summary ---"
grep -E 'alive_workers|firmware worker|registered process 4|not a child' "$LOG"
echo

# ---- assertions -------------------------------------------------------------

grep -q 'firmware worker 4 connected' "$LOG" \
    || fail "kernel never connected to the firmware worker"

grep -q 'registered process 4 (firmware' "$LOG" \
    || fail "firmware worker never completed the OP_REGISTER handshake"

# Before the kill: all four workers live, which means the firmware was
# answering both OP_ROUND_START (with a gradient) and OP_HEARTBEAT_PING.
grep -q 'alive_workers=4' "$LOG" \
    || fail "never reached 4 live workers -- firmware was not participating in rounds"

# After the kill: detected and dropped, not hung and not fatal.
grep -q 'alive_workers=3' "$LOG" \
    || fail "kernel did not drop to 3 workers after QEMU was killed"

# The whole point: the run continues. A detected fault degrades the system, it
# does not stop it.
grep -q 'all processes terminated, exiting' "$LOG" \
    || fail "kernel did not shut down cleanly after the fault"

# waitpid() must have been skipped for the non-child firmware entry.
grep -q 'not a child' "$LOG" \
    || fail "shutdown did not skip waitpid for the firmware worker"

echo
if [ "$FAILED" -eq 0 ]; then
    echo "PASS: firmware worker joined, trained, and its death was detected by the"
    echo "      kernel's existing heartbeat/fault-detection path."
    exit 0
fi

echo "--- full kernel log ---"
cat "$LOG"
exit 1
