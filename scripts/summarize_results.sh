#!/usr/bin/env bash
cd "$(dirname "$0")/.." || exit 1

echo "| Test | Protocol | Set latency | Set loss | Sent | Lost | Measured loss | Avg latency/RTT | Max |"
echo "|---|---|---|---|---|---|---|---|---|"
for file in results/test_*.txt; do
    [ -e "$file" ] || continue
    awk -F': ' '
        /^Test ID/ { id = $2 }
        /^Protocol/ { proto = $2 }
        /^Configured Latency/ { latency = $2 }
        /^Configured Packet Loss/ { loss = $2 }
        /^Packets Sent/ { sent = $2 }
        /^Packets Lost/ { lost = $2 }
        /^Packet Loss:/ { measured = $2 }
        /^Average (Latency|RTT)/ { average = $2 }
        /^Maximum (Latency|RTT)/ { maximum = $2 }
        END { printf "| %s | %s | %s | %s | %s | %s | %s | %s | %s |\n", id, proto, latency, loss, sent, lost, measured, average, maximum }
    ' "$file"
done
