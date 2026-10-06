#include "NetworkEmulator.h"
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <iostream>

namespace {

constexpr const char* kHostMac = "02:ca:05:00:00:01";
constexpr const char* kPeerMac = "02:ca:05:00:00:02";

bool overlapsExistingRoute(uint32_t addressNetworkOrder, std::string& conflictingInterface) {
    const uint32_t newMask = htonl(0xFFFFFF00u);
    const uint32_t newNetwork = addressNetworkOrder & newMask;

    std::ifstream routes("/proc/net/route");
    if (!routes) return false;

    std::string line;
    std::getline(routes, line);
    while (std::getline(routes, line)) {
        char name[32] = {0};
        unsigned destination = 0, gateway = 0, flags = 0, mask = 0;
        if (std::sscanf(line.c_str(), "%31s %x %x %x %*d %*d %*d %x",
                        name, &destination, &gateway, &flags, &mask) != 5)
            continue;
        if (!(flags & 1u) || mask == 0 || std::string(name) == NetworkEmulator::kHostIf) continue;

        uint32_t commonMask = mask & newMask;
        if ((destination & commonMask) == (newNetwork & commonMask)) {
            conflictingInterface = name;
            return true;
        }
    }
    return false;
}

}

bool NetworkEmulator::requireRoot() {
    if (geteuid() == 0) return true;
    std::cerr << "permission denied: run with sudo\n";
    return false;
}

bool NetworkEmulator::run(const std::vector<std::string>& args, bool quiet) {
    std::vector<char*> argv;
    for (const auto& arg : args) argv.push_back(const_cast<char*>(arg.c_str()));
    argv.push_back(nullptr);

    pid_t pid = fork();
    if (pid < 0) {
        std::cerr << "fork failed: " << std::strerror(errno) << "\n";
        return false;
    }
    if (pid == 0) {
        if (quiet) {
            int devNull = open("/dev/null", O_WRONLY);
            if (devNull >= 0) {
                dup2(devNull, STDOUT_FILENO);
                dup2(devNull, STDERR_FILENO);
            }
        }
        execvp(argv[0], argv.data());
        _exit(127);
    }

    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) return false;
    }
    if (!quiet && WIFEXITED(status) && WEXITSTATUS(status) == 127)
        std::cerr << "command not found: " << args[0] << "\n";
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

bool NetworkEmulator::teardown() {
    if (!requireRoot()) return false;
    run({"ip", "link", "del", kHostIf}, true);
    run({"ip", "netns", "del", kNamespace}, true);
    return true;
}

bool NetworkEmulator::validatePeerIp(const std::string& ip, std::string& error) {
    in_addr address{};
    if (inet_pton(AF_INET, ip.c_str(), &address) != 1) {
        error = "'" + ip + "' is not a valid IPv4 address (expected four numbers 0-255, like 10.200.0.2)";
        return false;
    }

    uint32_t host = ntohl(address.s_addr);
    uint32_t firstOctet = host >> 24;
    uint32_t lastOctet = host & 0xFFu;
    if (firstOctet == 0 || firstOctet == 127 || firstOctet >= 224) {
        error = "address " + ip + " is reserved (loopback, multicast or unspecified)";
        return false;
    }
    if (lastOctet == 0 || lastOctet == 1 || lastOctet == 255) {
        error = "the last number cannot be 0, 1 or 255 (.1 is used by the host side of the link)";
        return false;
    }

    std::string interfaceName;
    if (overlapsExistingRoute(address.s_addr, interfaceName)) {
        error = "'" + ip + "' overlaps an existing network route on '" + interfaceName +
                "' (using it could break your real connectivity)";
        return false;
    }
    return true;
}

bool NetworkEmulator::setup(const std::string& peerIp) {
    if (!requireRoot()) return false;

    std::string problem;
    if (!validatePeerIp(peerIp, problem)) {
        std::cerr << "invalid server IP: " << problem << "\n";
        return false;
    }

    in_addr peer{};
    inet_pton(AF_INET, peerIp.c_str(), &peer);
    in_addr host{};
    host.s_addr = (peer.s_addr & htonl(0xFFFFFF00u)) | htonl(1u);
    char hostText[INET_ADDRSTRLEN] = {0};
    inet_ntop(AF_INET, &host, hostText, sizeof(hostText));
    const std::string hostIp = hostText;

    teardown();

    const std::string ns = kNamespace;
    const std::vector<std::vector<std::string>> steps = {
        {"ip", "netns", "add", ns},
        {"ip", "link", "add", kHostIf, "address", kHostMac, "type", "veth", "peer", "name", kPeerIf, "address", kPeerMac},
        {"sysctl", "-q", "-w", std::string("net.ipv6.conf.") + kHostIf + ".disable_ipv6=1"},
        {"ip", "link", "set", kPeerIf, "netns", ns},
        {"ip", "netns", "exec", ns, "sysctl", "-q", "-w", std::string("net.ipv6.conf.") + kPeerIf + ".disable_ipv6=1"},
        {"ip", "addr", "add", hostIp + "/24", "dev", kHostIf},
        {"ip", "link", "set", kHostIf, "up"},
        {"ip", "netns", "exec", ns, "ip", "addr", "add", peerIp + "/24", "dev", kPeerIf},
        {"ip", "netns", "exec", ns, "ip", "link", "set", kPeerIf, "up"},
        {"ip", "netns", "exec", ns, "ip", "link", "set", "lo", "up"},
        {"ip", "neigh", "replace", peerIp, "lladdr", kPeerMac, "dev", kHostIf, "nud", "permanent"},
        {"ip", "netns", "exec", ns, "ip", "neigh", "replace", hostIp, "lladdr", kHostMac, "dev", kPeerIf, "nud", "permanent"},
    };

    for (const auto& step : steps) {
        if (!run(step)) {
            std::cerr << "setup failed at:";
            for (const auto& word : step) std::cerr << " " << word;
            std::cerr << "\n";
            teardown();
            return false;
        }
    }
    std::cout << "test network ready: " << hostIp << " <-> " << peerIp
              << " (namespace " << kNamespace << ")\n";
    return true;
}

bool NetworkEmulator::interfaceExists() const {
    std::string path = std::string("/sys/class/net/") + kHostIf;
    return access(path.c_str(), F_OK) == 0;
}

bool NetworkEmulator::setImpairment(int delayMs, double lossPercent) {
    if (!requireRoot()) return false;
    if (delayMs < 0 || delayMs > 10000) {
        std::cerr << "invalid latency: " << delayMs << " (expected 0-10000 ms)\n";
        return false;
    }
    if (!(lossPercent >= 0.0 && lossPercent <= 100.0)) {
        std::cerr << "invalid packet-loss: " << lossPercent << " (expected 0-100 %)\n";
        return false;
    }
    if (!interfaceExists()) {
        std::cerr << "network interface " << kHostIf
                  << " does not exist: the test network is not running\n";
        return false;
    }

    char lossText[32];
    std::snprintf(lossText, sizeof(lossText), "%.3f%%", lossPercent);
    bool ok = run({"tc", "qdisc", "replace", "dev", kHostIf, "root", "netem",
                   "delay", std::to_string(delayMs) + "ms", "loss", lossText});
    if (!ok) {
        std::cerr << "could not apply the netem rule: is the sch_netem kernel module available? "
                     "(try: sudo modprobe sch_netem)\n";
        return false;
    }
    std::cout << "netem set on " << kHostIf << ": delay " << delayMs
              << " ms, loss " << lossPercent << "%\n";
    return true;
}

bool NetworkEmulator::setDelay(int delayMs) {
    return setImpairment(delayMs, 0.0);
}

bool NetworkEmulator::setFromText(const char* delayText, const char* lossText) {
    char* end = nullptr;
    long delayMs = std::strtol(delayText, &end, 10);
    if (*delayText == '\0' || *end != '\0' || delayMs < 0 || delayMs > 10000) {
        std::cerr << "invalid latency: " << delayText << " (expected 0-10000 ms)\n";
        return false;
    }
    double lossPercent = std::strtod(lossText, &end);
    if (*lossText == '\0' || *end != '\0' || !(lossPercent >= 0.0 && lossPercent <= 100.0)) {
        std::cerr << "invalid packet-loss: " << lossText << " (expected 0-100 %)\n";
        return false;
    }
    return setImpairment(static_cast<int>(delayMs), lossPercent);
}

bool NetworkEmulator::clear() {
    if (!requireRoot()) return false;
    run({"tc", "qdisc", "del", "dev", kHostIf, "root"}, true);
    std::cout << "netem rules cleared\n";
    return true;
}

bool NetworkEmulator::show() {
    return run({"tc", "-s", "qdisc", "show", "dev", kHostIf});
}

bool handleEmulatorCommand(const std::string& mode, int argc, char** argv, int& result) {
    NetworkEmulator emulator;
    if (mode == "emu-setup" && argc == 2) {
        result = emulator.setup() ? 0 : 1;
        return true;
    }
    if (mode == "emu-teardown" && argc == 2) {
        result = emulator.teardown() ? 0 : 1;
        return true;
    }
    if (mode == "emu-clear" && argc == 2) {
        result = emulator.clear() ? 0 : 1;
        return true;
    }
    if (mode == "emu-show" && argc == 2) {
        result = emulator.show() ? 0 : 1;
        return true;
    }
    if (mode == "emu-delay" && argc == 3) {
        result = emulator.setFromText(argv[2], "0") ? 0 : 1;
        return true;
    }
    if (mode == "emu-set" && argc == 4) {
        result = emulator.setFromText(argv[2], argv[3]) ? 0 : 1;
        return true;
    }
    return false;
}

static uint64_t readCounterFile(const std::string& path) {
    std::ifstream file(path);
    uint64_t value = 0;
    file >> value;
    return value;
}

bool NetworkEmulator::runCapture(const std::vector<std::string>& args, std::string& output) {
    int pipeFds[2];
    if (pipe2(pipeFds, O_CLOEXEC) != 0) {
        std::cerr << "pipe failed: " << std::strerror(errno) << "\n";
        return false;
    }

    std::vector<char*> argv;
    for (const auto& arg : args) argv.push_back(const_cast<char*>(arg.c_str()));
    argv.push_back(nullptr);

    pid_t pid = fork();
    if (pid < 0) {
        std::cerr << "fork failed: " << std::strerror(errno) << "\n";
        close(pipeFds[0]);
        close(pipeFds[1]);
        return false;
    }
    if (pid == 0) {
        dup2(pipeFds[1], STDOUT_FILENO);
        int devNull = open("/dev/null", O_WRONLY);
        if (devNull >= 0) dup2(devNull, STDERR_FILENO);
        execvp(argv[0], argv.data());
        _exit(127);
    }

    close(pipeFds[1]);
    char buffer[1024];
    while (true) {
        ssize_t count = read(pipeFds[0], buffer, sizeof(buffer));
        if (count > 0) {
            output.append(buffer, static_cast<size_t>(count));
            continue;
        }
        if (count < 0 && errno == EINTR) continue;
        break;
    }
    close(pipeFds[0]);

    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) return false;
    }
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

bool NetworkEmulator::readKernelCounters(KernelCounters& counters) const {
    std::string output;
    if (!runCapture({"tc", "-s", "qdisc", "show", "dev", kHostIf}, output)) return false;

    counters = KernelCounters();
    counters.netemActive = output.find("qdisc netem") != std::string::npos;

    std::istringstream lines(output);
    std::string line;
    while (std::getline(lines, line)) {
        unsigned long long bytes = 0, packets = 0, dropped = 0;
        if (std::sscanf(line.c_str(), " Sent %llu bytes %llu pkt (dropped %llu",
                        &bytes, &packets, &dropped) == 3) {
            counters.sentPackets = packets;
            counters.droppedPackets = dropped;
            break;
        }
    }
    counters.txPackets = readCounterFile(std::string("/sys/class/net/") + kHostIf + "/statistics/tx_packets");
    return true;
}

std::string NetworkEmulator::formatKernelReport(const KernelCounters& before, const KernelCounters& after,
                                                uint32_t appLostPackets, bool reliableProtocol) {
    if (!after.netemActive) return "";

    auto delta = [](uint64_t later, uint64_t earlier) -> uint64_t {
        return later >= earlier ? later - earlier : 0;
    };
    const uint64_t dropped = delta(after.droppedPackets, before.droppedPackets);
    const uint64_t delivered = delta(after.sentPackets, before.sentPackets);
    const uint64_t transmitted = delta(after.txPackets, before.txPackets);

    std::ostringstream out;
    out << "\n---- Kernel view (" << kHostIf << ", this test) ----\n"
        << "Packets entering netem:      " << (dropped + delivered) << "\n"
        << "Dropped by netem (loss):     " << dropped << "\n"
        << "Passed on to the link:       " << delivered << "\n"
        << "Interface tx_packets:        " << transmitted << "\n"
        << "Application-measured loss:   " << appLostPackets;
    if (reliableProtocol) {
        out << " (TCP retransmitted what the kernel dropped)\n";
        return out.str();
    }
    out << "\n";

    const long long difference = static_cast<long long>(dropped) - static_cast<long long>(appLostPackets);
    if (difference == 0)
        out << "Cross-check: kernel and application agree\n";
    else if (difference > 0)
        out << "Cross-check: kernel dropped " << difference
            << " more (background packets such as IPv6 neighbor discovery)\n";
    else
        out << "Cross-check: application lost " << -difference
            << " more than netem dropped (drops outside netem, e.g. a full socket buffer)\n";
    return out.str();
}
