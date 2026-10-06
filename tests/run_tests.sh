#!/usr/bin/env bash
set -u
cd "$(dirname "$0")/.." || exit 1

if [ "$(id -u)" -ne 0 ]; then
    echo "run with sudo: sudo ./tests/run_tests.sh"
    exit 1
fi
if [ ! -x ./chaos ]; then
    echo "build first with: make"
    exit 1
fi

mkdir -p results
REPORT=results/suite_report.txt
PASS=0
FAIL=0
OUT=""
RC=0

{
    echo "Network Chaos Emulator test suite"
    echo "Date:   $(date '+%Y-%m-%d %H:%M:%S')"
    echo "Kernel: $(uname -r)"
    echo "g++:    $(g++ --version | head -1)"
    echo
} > "$REPORT"
cat "$REPORT"

run_menu() {
    OUT=$(printf '%b' "$1" | timeout 240 ./chaos 2>&1)
    RC=$?
}

has() {
    printf '%s\n' "$OUT" | grep -qF -- "$1"
}

field() {
    printf '%s\n' "$1" | grep -m1 "^$2:" | sed -nE 's/^[^:]*: *([0-9.]+).*/\1/p'
}

between() {
    awk -v v="$1" -v lo="$2" -v hi="$3" 'BEGIN { exit !(v ~ /^[0-9.]+$/ && v + 0 >= lo + 0 && v + 0 <= hi + 0) }'
}

saved_file() {
    printf '%s\n' "$OUT" | grep -o 'results/test_[0-9]*\.txt' | head -1
}

record() {
    local name="$1" status="$2" detail="$3"
    printf '%-5s %-36s %s\n' "$status" "$name" "$detail" | tee -a "$REPORT"
    if [ "$status" = PASS ]; then PASS=$((PASS + 1)); else FAIL=$((FAIL + 1)); fi
}

udp_case() {
    local name="$1" lat="$2" loss="$3" count="$4" latlo="$5" lathi="$6" lostlo="$7" losthi="$8"
    run_menu "2\n2\n$lat\n3\n$loss\n4\n$count\n10\n1\n3\n5\n7\n"
    local sent lost avg kdrop close
    sent=$(field "$OUT" "Packets Sent")
    lost=$(field "$OUT" "Packets Lost")
    avg=$(field "$OUT" "Average Latency")
    kdrop=$(field "$OUT" "Dropped by netem (loss)")
    close=$(awk -v a="$kdrop" -v b="$lost" 'BEGIN { if (a ~ /^[0-9.]+$/ && b ~ /^[0-9.]+$/) { d = a - b; if (d < 0) d = -d; print (d <= 3) ? "yes" : "no" } else print "no" }')
    local detail="sent=$sent lost=$lost avg=${avg}ms kernel_dropped=${kdrop:-none} file=$(saved_file)"
    if [ "$RC" -eq 0 ] && [ "$sent" = "$count" ] && between "$lost" "$lostlo" "$losthi" \
        && between "$avg" "$latlo" "$lathi" && [ "$close" = yes ]; then
        record "$name" PASS "$detail"
    else
        record "$name" FAIL "$detail (wanted lost $lostlo-$losthi, avg $latlo-$lathi, exit 0, kernel within 3)"
    fi
}

cli_case() {
    local name="$1" expected="$2"
    shift 2
    OUT=$("$@" 2>&1)
    RC=$?
    if [ "$RC" -ne 0 ] && has "$expected"; then
        record "$name" PASS "exit=$RC: $expected"
    else
        record "$name" FAIL "exit=$RC, wanted '$expected', got: $(printf '%s' "$OUT" | head -1)"
    fi
}

udp_case "01 0 ms / 0% loss" 0 0 300 0 5 0 0
udp_case "02 50 ms / 0% loss" 50 0 300 50 55 0 0
udp_case "03 100 ms / 5% loss" 100 5 300 100 105 1 36
udp_case "04 200 ms / 10% loss" 200 10 300 200 205 10 55
udp_case "05 500 ms / 20% loss" 500 20 300 500 505 30 95
udp_case "06a high loss 50%" 0 50 300 0 5 100 200
udp_case "06b high loss 90%" 0 90 300 0 5 250 299

run_menu 'abc\n9\n2\n2\nabc\n99999\n\n3\n150\nnan\n5x\n\n4\n0\n\n5\n5\n\n8\n0\n70000\n\n9\n999.1.1.1\n127.0.0.1\n10.200.0.1\n\n10\n7\n'
missing=""
for expected in \
    'invalid choice: enter a number from 1 to 7' \
    'invalid value: expected a whole number from 0 to 10000' \
    'invalid value: expected a number from 0 to 100' \
    'invalid value: expected a whole number from 1 to 1000000' \
    'invalid value: expected a whole number from 12 to 1472' \
    'invalid value: expected a whole number from 1 to 65535' \
    'is not a valid IPv4 address' \
    'is reserved' \
    'last number cannot be 0, 1 or 255' \
    'test network removed'; do
    has "$expected" || missing="$missing [$expected]"
done
if [ "$RC" -eq 0 ] && [ -z "$missing" ] && ! has 'terminate called' && ! has 'Segmentation'; then
    record "07a invalid menu input" PASS "all error messages shown, clean exit"
else
    record "07a invalid menu input" FAIL "exit=$RC missing:$missing"
fi
cli_case "07b invalid loss on command line" "invalid packet-loss: 150" ./chaos emu-set 0 150
cli_case "07c invalid latency on command line" "invalid latency: abc" ./chaos emu-delay abc

cli_case "08a server unavailable" "server unavailable at 127.0.0.1:59999" ./chaos client 127.0.0.1 59999 hi
run_menu '3\n7\n'
if [ "$RC" -eq 0 ] && has 'server is not running'; then
    record "08b test without server" PASS "refused politely, clean exit"
else
    record "08b test without server" FAIL "exit=$RC"
fi

cli_case "09a client port too large" "invalid port: 70000" ./chaos client 127.0.0.1 70000 hi
cli_case "09b client port not a number" "invalid port: abc" ./chaos client 127.0.0.1 abc hi
cli_case "09c server port zero" "invalid port: 0" ./chaos server 0
cli_case "09d udp-recv port zero" "invalid port: 0" ./chaos udp-recv 0 10 500

./chaos server 9100 > /dev/null 2>&1 &
BACKGROUND=$!
sleep 1
OUT=$(./chaos server 9100 2>&1)
RC=$?
kill "$BACKGROUND" 2> /dev/null
wait "$BACKGROUND" 2> /dev/null
if [ "$RC" -ne 0 ] && has 'port 9100 is already in use'; then
    record "09e port already in use" PASS "second server refused"
else
    record "09e port already in use" FAIL "exit=$RC: $(printf '%s' "$OUT" | head -1)"
fi

run_menu '2\n4\n100\n10\n1\n3\n1\n3\n1\n3\n5\n7\n'
blocks=$(printf '%s\n' "$OUT" | grep -c 'Test Results (UDP)')
zero=$(printf '%s\n' "$OUT" | grep -c '^Packets Lost: 0$')
ready=$(printf '%s\n' "$OUT" | grep -c 'server ready')
if [ "$RC" -eq 0 ] && [ "$blocks" -eq 3 ] && [ "$zero" -eq 3 ] && [ "$ready" -eq 3 ]; then
    record "10 three consecutive tests" PASS "3 servers, 3 result blocks, 0 lost each, file=$(saved_file)"
else
    record "10 three consecutive tests" FAIL "exit=$RC servers=$ready blocks=$blocks zero_loss=$zero"
fi

run_menu '2\n1\n2\n4\n50\n3\n20\n10\n1\n3\n5\n7\n'
sent=$(field "$OUT" "Packets Sent")
lost=$(field "$OUT" "Packets Lost")
maxrtt=$(field "$OUT" "Maximum RTT")
kdrop=$(field "$OUT" "Dropped by netem (loss)")
detail="sent=$sent lost=$lost max_rtt=${maxrtt}ms kernel_dropped=${kdrop:-none} file=$(saved_file)"
if [ "$RC" -eq 0 ] && [ "$sent" = 50 ] && [ "$lost" = 0 ] && between "$maxrtt" 150 100000 && between "$kdrop" 1 100000; then
    record "11 TCP at 20% loss" PASS "$detail"
else
    record "11 TCP at 20% loss" FAIL "$detail"
fi

if ip netns list | grep -q chaosns || ip link show chaos0 > /dev/null 2>&1; then
    record "12 cleanup" FAIL "test network was left behind"
else
    record "12 cleanup" PASS "no leftover namespace or interface"
fi

echo | tee -a "$REPORT"
echo "PASSED: $PASS   FAILED: $FAIL" | tee -a "$REPORT"
if [ -n "${SUDO_UID:-}" ]; then
    chown "$SUDO_UID:${SUDO_GID:-$SUDO_UID}" "$REPORT"
fi
[ "$FAIL" -eq 0 ]
