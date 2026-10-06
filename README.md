# Network Latency & Packet-Loss Chaos Emulator

A Linux/C++17 tool that builds an isolated test network, impairs it with real kernel-level latency and packet loss, sends real UDP/TCP traffic across it, and measures what actually arrives.

Nothing is simulated in user space. Delay and loss are applied by the Linux kernel (`tc netem`), and every statistic comes from packets that really crossed the link.

## How it works

    user space      Menu -> TestRunner -> sender (main thread)      receiver (thread in namespace)
                                |  sendto()                                ^  recvfrom()
    system calls  ==============|==========================================|=====================
    kernel                      v                                          |
                    socket -> UDP/TCP -> IP -> routing -> qdisc (netem: delay + loss)
                    -> veth chaos0 ==================> veth chaos1 -> IP -> UDP/TCP -> socket
                    (host namespace)                  (namespace "chaosns")

- On start the program creates a network namespace (`chaosns`) and a virtual Ethernet pair (`chaos0` in the host, `chaos1` in the namespace) using `ip`.
- `tc qdisc replace dev chaos0 root netem delay Xms loss Y%` makes the kernel hold each outgoing packet for X ms and randomly drop Y% of them.
- The server runs in a thread that has entered the namespace with `setns()`. The sender runs in the host namespace, so every packet crosses the impaired link.
- Statistics are computed from sequence numbers and timestamps carried in each packet. After every test the program also reads the kernel's own drop counters (`tc -s qdisc`, `/sys/class/net`) and cross-checks them against what the application measured.

## Requirements

- Ubuntu (tested on Ubuntu 24.04 under WSL2, kernel 6.18)
- g++ with C++17, make
- `iproute2` (`ip`, `tc`) and the `sch_netem` kernel module
- root (`sudo`), because creating namespaces and qdiscs needs `CAP_NET_ADMIN`

## Build and run

    make
    sudo ./chaos            # interactive menu

## Menu

| Option | What it does |
|---|---|
| 1. Start Server | Starts a UDP receiver or TCP echo server inside the test network |
| 2. Configure Network Conditions | Protocol, latency, loss, packet count, packet size, interval, time limit, port, server IP |
| 3. Start Network Test | Sends traffic through the impaired link and shows results |
| 4. Display Statistics | Shows the last test and its configuration |
| 5. Save Results | Writes `results/test_NNN.txt` (never overwrites) |
| 6. Reset Configuration | Restores defaults and removes the netem rule |
| 7. Exit | Removes the test network |

Latency and loss take effect the moment they are set. While a test runs on a terminal, a monitoring thread prints a live progress line.

## Statistics

Packets sent, received and lost; loss percentage; minimum, maximum and average latency (one-way for UDP, round-trip time for TCP); test duration; throughput.

UDP shows loss as missing packets. TCP hides loss by retransmitting, so under loss it shows `Packets Lost: 0` with higher round-trip times, while the kernel block shows how many packets netem dropped.

## Command-line modes

    sudo ./chaos emu-setup | emu-teardown | emu-show | emu-clear
    sudo ./chaos emu-set <delay_ms> <loss_percent>
    ./chaos server <port>                                 # basic TCP echo server
    ./chaos client <ip> <port> <message>                  # basic TCP client
    ./chaos udp-send <ip> <port> <count> <size> <interval_ms>
    ./chaos udp-recv <port> <count> <idle_ms>

## Project layout

    src/ include/    modules: Menu, NetworkConfig, NetworkEmulator, TestRunner, TrafficGenerator,
                     UdpReceiver, TcpEcho, Statistics, LiveProgress, ProgressMonitor, Logger, Packet
    tests/           run_tests.sh, the automated suite
    scripts/         summarize_results.sh
    results/         saved test results and the suite report
    docs/            results.md, generated table of recorded runs

## Testing

    make
    sudo ./tests/run_tests.sh

The suite covers latency and loss combinations, high loss, invalid configuration, unavailable server, invalid and busy ports, consecutive tests, TCP under loss and cleanup. Each loss test accepts a statistical band, and the UDP tests also require the kernel drop counter to match the application's loss count. Recorded runs are in `results/` and summarized in `docs/results.md`.

## Measurement problems found and fixed

- **Wall-clock steps:** timestamps from the system clock produced latency spikes of 1.5 s when the clock was adjusted. Timestamps now use the monotonic clock.
- **ARP:** the first packets on a new link waited for an ARP exchange that netem itself delayed. The link now uses fixed MAC addresses and permanent neighbor entries.
- **IPv6 background traffic:** extra packets entering netem made the kernel's drop count exceed the application's. IPv6 is disabled on the test link, and the counts now match exactly.

## About the device-driver question

A custom kernel module is not part of this project. The kernel component doing the work is `sch_netem`, which already exists in Linux, and the program reaches it through system calls, the `ip`/`tc` tools (which talk to the kernel over netlink), and `/proc` and `/sys`. The C++ code runs entirely in user space.

## Limitations

- Delay and loss are applied in the host-to-server direction only, so TCP round-trip time equals the configured latency, not twice that.
- The TCP test is a stop-and-wait echo, so it measures round-trip time and not maximum throughput.
- IPv4 only. Needs root. Timing under WSL2 can show occasional scheduling jitter.
