#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>

class TcpEchoServer {
public:
    TcpEchoServer() = default;
    ~TcpEchoServer();
    TcpEchoServer(const TcpEchoServer&) = delete;
    TcpEchoServer& operator=(const TcpEchoServer&) = delete;

    bool open(uint16_t port);
    void serve();
    void requestStop() { stopRequested_ = true; }

private:
    void closeListener();
    void echoClient(int clientFd);

    int listenFd_ = -1;
    std::atomic<bool> stopRequested_{false};
};

class TcpEchoClient {
public:
    TcpEchoClient() = default;
    ~TcpEchoClient();
    TcpEchoClient(const TcpEchoClient&) = delete;
    TcpEchoClient& operator=(const TcpEchoClient&) = delete;

    bool connectTo(const std::string& ip, uint16_t port, int timeoutMs);
    bool exchange(const uint8_t* data, size_t length, uint8_t* reply, int timeoutMs);
    void disconnect();

private:
    int fd_ = -1;
};
