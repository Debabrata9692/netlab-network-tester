#pragma once
#include "LiveProgress.h"
#include "NetworkConfig.h"
#include "Statistics.h"
#include "TcpEcho.h"
#include "UdpReceiver.h"
#include <cstdint>
#include <future>
#include <memory>
#include <string>
#include <thread>

struct TestResult {
    NetworkConfig config;
    Statistics stats;
    std::string report;
};

class TestRunner {
public:
    TestRunner() = default;
    ~TestRunner();
    TestRunner(const TestRunner&) = delete;
    TestRunner& operator=(const TestRunner&) = delete;

    bool startServer(const NetworkConfig& config);
    void stopServer();
    bool serverRunning() const { return serverThread_.joinable(); }
    bool runTest(const NetworkConfig& config, TestResult& result);

private:
    void udpServerMain(uint16_t port, uint32_t expectedCount, std::promise<bool> ready);
    void tcpServerMain(uint16_t port, std::promise<bool> ready);
    bool runUdpTest(const NetworkConfig& config, TestResult& result);
    bool runTcpTest(const NetworkConfig& config, TestResult& result);

    LiveProgress progress_;
    std::unique_ptr<UdpReceiver> receiver_;
    std::unique_ptr<TcpEchoServer> tcpServer_;
    Protocol serverProtocol_ = Protocol::Udp;
    std::thread serverThread_;
};
