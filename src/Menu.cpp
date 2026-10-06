#include "Menu.h"
#include <unistd.h>
#include <exception>
#include <iostream>

Menu::~Menu() {
    runner_.stopServer();
    if (networkUp_) {
        emulator_.teardown();
        std::cout << "test network removed\n";
    }
}

int Menu::run() {
    if (geteuid() != 0) {
        std::cerr << "permission denied: the emulator creates a test network, run with sudo ./chaos\n";
        return 1;
    }
    if (!emulator_.setup(config_.serverIp)) return 1;
    networkUp_ = true;

    while (!exitRequested_ && !inputClosed_) {
        printMenu();
        std::string line;
        if (!readLine(line)) break;
        long choice = 0;
        if (!parseLong(line, 1, 7, choice)) {
            std::cout << "invalid choice: enter a number from 1 to 7\n";
            continue;
        }
        try {
            handleChoice(static_cast<int>(choice));
        } catch (const std::exception& error) {
            std::cerr << "unexpected error: " << error.what() << "\n";
        }
    }
    return 0;
}

void Menu::printMenu() const {
    std::cout << "\n========================================\n"
              << " NETWORK LATENCY & PACKET-LOSS EMULATOR\n"
              << "========================================\n\n"
              << "1. Start Server\n"
              << "2. Configure Network Conditions\n"
              << "3. Start Network Test\n"
              << "4. Display Statistics\n"
              << "5. Save Results\n"
              << "6. Reset Configuration\n"
              << "7. Exit\n\n"
              << "Enter choice: " << std::flush;
}

void Menu::printConfigureMenu() const {
    std::cout << "\n---- Configure Network Conditions ----\n"
              << config_.toString()
              << "\n1. Protocol (UDP/TCP)\n"
              << "2. Latency (ms)\n"
              << "3. Packet loss (%)\n"
              << "4. Packet count\n"
              << "5. Packet size (bytes)\n"
              << "6. Send interval (ms)\n"
              << "7. Time limit (s)\n"
              << "8. Port\n"
              << "9. Server IP\n"
              << "10. Back\n\n"
              << "Enter choice: " << std::flush;
}

void Menu::handleChoice(int choice) {
    switch (choice) {
        case 1: startServer(); break;
        case 2: configureMenu(); break;
        case 3: startTest(); break;
        case 4: displayStatistics(); break;
        case 5: saveResults(); break;
        case 6: resetConfiguration(); break;
        case 7: exitRequested_ = true; break;
        default: break;
    }
}

void Menu::notAvailable(const char* feature) const {
    std::cout << feature << ": not implemented yet\n";
}

void Menu::startServer() {
    if (runner_.serverRunning()) {
        std::cout << "server is already running\n";
        return;
    }
    if (runner_.startServer(config_)) {
        std::cout << "server ready: " << protocolName(config_.protocol) << " on "
                  << config_.serverIp << ":" << config_.port << " inside the test network\n";
    }
}

void Menu::startTest() {
    TestResult result;
    NetworkEmulator::KernelCounters before;
    const bool haveBefore = emulator_.readKernelCounters(before);
    if (!runner_.runTest(config_, result)) return;
    NetworkEmulator::KernelCounters after;
    const bool haveAfter = emulator_.readKernelCounters(after);
    kernelReport_ = (haveBefore && haveAfter)
        ? NetworkEmulator::formatKernelReport(before, after, result.stats.packetsLost(),
                                              result.config.protocol == Protocol::Tcp)
        : std::string();
    lastResult_ = result;
    haveResult_ = true;
    savedPath_.clear();
    std::cout << "\n---- Test Results (" << protocolName(result.config.protocol) << ") ----\n"
              << result.report << kernelReport_;
}

void Menu::displayStatistics() const {
    if (!haveResult_) {
        std::cout << "no test has been run yet: choose 1, then 3\n";
        return;
    }
    std::cout << "\n---- Last Test ----\n"
              << lastResult_.config.toString()
              << "\n"
              << lastResult_.report << kernelReport_;
}

void Menu::saveResults() {
    if (!haveResult_) {
        std::cout << "no test has been run yet: choose 1, then 3\n";
        return;
    }
    if (!savedPath_.empty()) {
        std::cout << "already saved: " << savedPath_ << "\n";
        return;
    }
    std::string path;
    if (logger_.save(lastResult_.config, lastResult_.stats, path, kernelReport_)) {
        savedPath_ = path;
        std::cout << "results saved to " << path << "\n";
    }
}

void Menu::stopServerAfterChange() {
    if (!runner_.serverRunning()) return;
    runner_.stopServer();
    std::cout << "server stopped because the settings changed: choose 1 to start it again\n";
}

void Menu::configureMenu() {
    bool back = false;
    while (!back && !inputClosed_) {
        printConfigureMenu();
        std::string line;
        if (!readLine(line)) return;
        long choice = 0;
        if (!parseLong(line, 1, 10, choice)) {
            std::cout << "invalid choice: enter a number from 1 to 10\n";
            continue;
        }
        switch (choice) {
            case 1: editProtocol(); break;
            case 2: editLatency(); break;
            case 3: editLoss(); break;
            case 4: editPacketCount(); break;
            case 5: editPacketSize(); break;
            case 6: editInterval(); break;
            case 7: editDuration(); break;
            case 8: editPort(); break;
            case 9: editServerIp(); break;
            case 10: back = true; break;
            default: break;
        }
    }
}

void Menu::resetConfiguration() {
    stopServerAfterChange();
    const std::string previousIp = config_.serverIp;
    config_.reset();
    if (previousIp != config_.serverIp) {
        if (!emulator_.setup(config_.serverIp))
            std::cout << "could not restore the default test network\n";
    } else {
        emulator_.clear();
    }
    std::cout << "configuration reset to defaults\n";
}

bool Menu::readLine(std::string& line) {
    if (std::getline(std::cin, line)) return true;
    inputClosed_ = true;
    std::cout << "\n";
    return false;
}

bool Menu::promptLong(const std::string& label, long min, long max, long& value) {
    while (true) {
        std::cout << label << " [" << min << "-" << max << ", Enter to cancel]: " << std::flush;
        std::string line;
        if (!readLine(line)) return false;
        if (trimWhitespace(line).empty()) return false;
        if (parseLong(line, min, max, value)) return true;
        std::cout << "invalid value: expected a whole number from " << min << " to " << max << "\n";
    }
}

bool Menu::promptDouble(const std::string& label, double min, double max, double& value) {
    while (true) {
        std::cout << label << " [" << min << "-" << max << ", Enter to cancel]: " << std::flush;
        std::string line;
        if (!readLine(line)) return false;
        if (trimWhitespace(line).empty()) return false;
        if (parseDouble(line, min, max, value)) return true;
        std::cout << "invalid value: expected a number from " << min << " to " << max
                  << " (digits and one decimal point only)\n";
    }
}

bool Menu::applyNetem() {
    return emulator_.setImpairment(config_.latencyMs, config_.lossPercent);
}

void Menu::editProtocol() {
    long value = 0;
    if (!promptLong("Protocol: 1 = UDP, 2 = TCP", 1, 2, value)) return;
    Protocol chosen = value == 1 ? Protocol::Udp : Protocol::Tcp;
    if (chosen == config_.protocol) return;
    config_.protocol = chosen;
    stopServerAfterChange();
}

void Menu::editLatency() {
    long value = 0;
    if (!promptLong("Latency in ms (e.g. 0, 50, 100, 200, 500)", 0, NetworkConfig::kMaxLatencyMs, value)) return;
    int previous = config_.latencyMs;
    config_.latencyMs = static_cast<int>(value);
    if (!applyNetem()) config_.latencyMs = previous;
}

void Menu::editLoss() {
    double value = 0.0;
    if (!promptDouble("Packet loss in % (e.g. 0, 1, 5, 10, 20, 50)", 0.0, 100.0, value)) return;
    double previous = config_.lossPercent;
    config_.lossPercent = value;
    if (!applyNetem()) config_.lossPercent = previous;
}

void Menu::editPacketCount() {
    long value = 0;
    if (!promptLong("Packet count", 1, NetworkConfig::kMaxPacketCount, value)) return;
    if (static_cast<uint32_t>(value) == config_.packetCount) return;
    config_.packetCount = static_cast<uint32_t>(value);
    stopServerAfterChange();
}

void Menu::editPacketSize() {
    long value = 0;
    if (promptLong("Packet size in bytes", NetworkConfig::kMinSize, NetworkConfig::kMaxSize, value))
        config_.packetSize = static_cast<uint32_t>(value);
}

void Menu::editInterval() {
    long value = 0;
    if (promptLong("Send interval in ms", 0, NetworkConfig::kMaxIntervalMs, value))
        config_.intervalMs = static_cast<uint32_t>(value);
}

void Menu::editDuration() {
    long value = 0;
    if (promptLong("Time limit in seconds (0 = no limit)", 0, NetworkConfig::kMaxDurationSec, value))
        config_.durationSec = static_cast<uint32_t>(value);
}

void Menu::editPort() {
    long value = 0;
    if (!promptLong("Port", 1, 65535, value)) return;
    if (static_cast<uint16_t>(value) == config_.port) return;
    config_.port = static_cast<uint16_t>(value);
    stopServerAfterChange();
}

void Menu::editServerIp() {
    while (true) {
        std::cout << "Server IP, e.g. 10.200.0.2 [current " << config_.serverIp
                  << ", Enter to cancel]: " << std::flush;
        std::string line;
        if (!readLine(line)) return;
        std::string ip = trimWhitespace(line);
        if (ip.empty() || ip == config_.serverIp) return;

        std::string problem;
        if (!NetworkEmulator::validatePeerIp(ip, problem)) {
            std::cout << "invalid server IP: " << problem << "\n";
            continue;
        }

        stopServerAfterChange();
        const std::string previous = config_.serverIp;
        if (emulator_.setup(ip)) {
            config_.serverIp = ip;
        } else {
            std::cout << "could not move the test network to " << ip << ": restoring " << previous << "\n";
            if (!emulator_.setup(previous))
                std::cout << "the test network is down: choose 7 to exit, then start again\n";
        }
        if (config_.latencyMs > 0 || config_.lossPercent > 0.0) applyNetem();
        return;
    }
}
