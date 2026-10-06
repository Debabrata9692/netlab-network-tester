#include "TestRunner.h"
#include "NetworkEmulator.h"
#include "Packet.h"
#include "ProgressMonitor.h"
#include "TrafficGenerator.h"
#include <fcntl.h>
#include <sched.h>
#include <unistd.h>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>

namespace {

constexpr int kGraceAfterSendMs = 1000;
constexpr int kTcpConnectTimeoutMs = 10000;
constexpr int kTcpExchangeTimeoutMs = 10000;

bool enterTestNamespace() {
    std::string path = std::string("/var/run/netns/") + NetworkEmulator::kNamespace;
    int nsFd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (nsFd < 0) {
        std::cerr << "cannot open " << path << ": " << std::strerror(errno)
                  << " (is the test network up?)\n";
        return false;
    }
    int result = setns(nsFd, CLONE_NEWNET);
    int savedErrno = errno;
    close(nsFd);
    if (result != 0) {
        std::cerr << "setns failed: " << std::strerror(savedErrno) << "\n";
        return false;
    }
    return true;
}

}

TestRunner::~TestRunner() { stopServer(); }

void TestRunner::udpServerMain(uint16_t port, uint32_t expectedCount, std::promise<bool> ready) {
    if (!enterTestNamespace() || !receiver_->open(port)) {
        ready.set_value(false);
        return;
    }
    ready.set_value(true);
    receiver_->receive(expectedCount, 0);
}

void TestRunner::tcpServerMain(uint16_t port, std::promise<bool> ready) {
    if (!enterTestNamespace() || !tcpServer_->open(port)) {
        ready.set_value(false);
        return;
    }
    ready.set_value(true);
    tcpServer_->serve();
}

bool TestRunner::startServer(const NetworkConfig& config) {
    if (serverRunning()) return true;

    std::promise<bool> ready;
    std::future<bool> readyResult = ready.get_future();
    serverProtocol_ = config.protocol;
    if (config.protocol == Protocol::Udp) {
        receiver_ = std::make_unique<UdpReceiver>();
        receiver_->setProgress(&progress_);
        serverThread_ = std::thread(&TestRunner::udpServerMain, this, config.port,
                                    config.packetCount, std::move(ready));
    } else {
        tcpServer_ = std::make_unique<TcpEchoServer>();
        serverThread_ = std::thread(&TestRunner::tcpServerMain, this, config.port, std::move(ready));
    }

    if (!readyResult.get()) {
        serverThread_.join();
        receiver_.reset();
        tcpServer_.reset();
        return false;
    }
    return true;
}

void TestRunner::stopServer() {
    if (!serverThread_.joinable()) return;
    if (receiver_) receiver_->requestStop();
    if (tcpServer_) tcpServer_->requestStop();
    serverThread_.join();
    receiver_.reset();
    tcpServer_.reset();
}

bool TestRunner::runTest(const NetworkConfig& config, TestResult& result) {
    if (!serverRunning()) {
        std::cerr << "server is not running: choose 1 (Start Server) first\n";
        return false;
    }
    if (config.protocol != serverProtocol_) {
        std::cerr << "server was started for " << protocolName(serverProtocol_)
                  << ": start it again (option 1)\n";
        stopServer();
        return false;
    }
    return config.protocol == Protocol::Udp ? runUdpTest(config, result)
                                            : runTcpTest(config, result);
}

bool TestRunner::runUdpTest(const NetworkConfig& config, TestResult& result) {
    UdpSender sender;
    if (!sender.open(config.serverIp, config.port)) {
        stopServer();
        return false;
    }

    std::cout << "sending up to " << config.packetCount << " UDP packets of " << config.packetSize
              << " bytes to " << config.serverIp << ":" << config.port;
    if (config.durationSec > 0) std::cout << " (time limit " << config.durationSec << " s)";
    std::cout << " ...\n";

    progress_.reset(config.packetCount);
    sender.setProgress(&progress_);
    ProgressMonitor monitor(progress_);

    bool sentOk = sender.sendPackets(config.packetCount, config.packetSize, config.intervalMs,
                                     config.durationSec * 1000u);
    if (!sentOk) {
        monitor.stop();
        std::cerr << "sending failed after " << sender.sentCount() << " packets\n";
        stopServer();
        return false;
    }

    receiver_->notifySenderDone(config.latencyMs + kGraceAfterSendMs);
    serverThread_.join();
    monitor.stop();
    receiver_->setSentCount(sender.sentCount());

    result.config = config;
    result.stats = receiver_->stats();
    result.report = receiver_->report();
    receiver_.reset();
    return true;
}

bool TestRunner::runTcpTest(const NetworkConfig& config, TestResult& result) {
    TcpEchoClient client;
    if (!client.connectTo(config.serverIp, config.port, kTcpConnectTimeoutMs)) {
        stopServer();
        return false;
    }

    std::cout << "exchanging up to " << config.packetCount << " TCP messages of " << config.packetSize
              << " bytes with " << config.serverIp << ":" << config.port;
    if (config.durationSec > 0) std::cout << " (time limit " << config.durationSec << " s)";
    std::cout << " ...\n";

    progress_.reset(config.packetCount);
    ProgressMonitor monitor(progress_);

    Statistics stats;
    stats.setLatencyLabel("RTT");
    std::vector<uint8_t> message(config.packetSize, 0);
    std::vector<uint8_t> reply(config.packetSize, 0);
    uint32_t sent = 0;
    bool failed = false;
    const auto started = std::chrono::steady_clock::now();

    for (uint32_t sequence = 1; sequence <= config.packetCount; ++sequence) {
        if (config.durationSec > 0 &&
            std::chrono::steady_clock::now() - started >= std::chrono::seconds(config.durationSec))
            break;

        encodeHeader(message.data(), sequence, nowMicros());
        ++sent;
        progress_.setSent(sent);
        if (!client.exchange(message.data(), message.size(), reply.data(), kTcpExchangeTimeoutMs)) {
            failed = true;
            break;
        }

        uint64_t arrivalUs = nowMicros();
        uint32_t echoedSequence = 0;
        uint64_t sentUs = 0;
        if (!decodeHeader(reply.data(), reply.size(), echoedSequence, sentUs) ||
            echoedSequence != sequence) {
            monitor.stop();
            std::cerr << "corrupted echo for message " << sequence << "\n";
            failed = true;
            break;
        }
        int64_t rttUs = static_cast<int64_t>(arrivalUs) - static_cast<int64_t>(sentUs);
        stats.recordReceived(rttUs, message.size(), arrivalUs);
        progress_.addReceived(rttUs);

        if (config.intervalMs > 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(config.intervalMs));
    }

    monitor.stop();
    stats.setPacketsSent(sent);
    client.disconnect();
    serverThread_.join();
    receiver_.reset();
    tcpServer_.reset();

    if (failed) std::cerr << "TCP test stopped early after " << sent << " messages\n";
    result.config = config;
    result.stats = stats;
    result.report = stats.summary();
    return true;
}
