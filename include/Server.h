#pragma once
#include <atomic>
#include <cstdint>
#include <netinet/in.h>

class TcpServer {
public:
    TcpServer() = default;
    ~TcpServer();
    TcpServer(const TcpServer&) = delete;
    TcpServer& operator=(const TcpServer&) = delete;

    bool start(uint16_t port);
    void run();
    void stop();
    static void requestStop();

private:
    void handleClient(int clientFd, const sockaddr_in& peer);

    int listenFd_ = -1;
    static std::atomic<bool> stopRequested_;
};
