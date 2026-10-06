#pragma once
#include <cstdint>
#include <string>

class TcpClient {
public:
    TcpClient() = default;
    ~TcpClient();
    TcpClient(const TcpClient&) = delete;
    TcpClient& operator=(const TcpClient&) = delete;

    bool connectTo(const std::string& ip, uint16_t port);
    bool sendMessage(const std::string& message);
    bool receiveMessage(std::string& message);
    void disconnect();

private:
    int fd_ = -1;
};
