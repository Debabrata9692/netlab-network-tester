#include "Client.h"
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <iostream>

TcpClient::~TcpClient() { disconnect(); }

void TcpClient::disconnect() {
    if (fd_ >= 0) {
        close(fd_);
        fd_ = -1;
    }
}

bool TcpClient::connectTo(const std::string& ip, uint16_t port) {
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) != 1) {
        std::cerr << "invalid IP address: " << ip << "\n";
        return false;
    }

    fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (fd_ < 0) {
        std::cerr << "socket failed: " << std::strerror(errno) << "\n";
        return false;
    }

    if (connect(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        if (errno == ECONNREFUSED)
            std::cerr << "server unavailable at " << ip << ":" << port << "\n";
        else
            std::cerr << "connect failed: " << std::strerror(errno) << "\n";
        disconnect();
        return false;
    }
    return true;
}

bool TcpClient::sendMessage(const std::string& message) {
    size_t total = 0;
    while (total < message.size()) {
        ssize_t sent = send(fd_, message.data() + total, message.size() - total, MSG_NOSIGNAL);
        if (sent < 0) {
            if (errno == EINTR) continue;
            std::cerr << "send failed: " << std::strerror(errno) << "\n";
            return false;
        }
        total += static_cast<size_t>(sent);
    }
    return true;
}

bool TcpClient::receiveMessage(std::string& message) {
    char buffer[1024];
    ssize_t received;
    do {
        received = recv(fd_, buffer, sizeof(buffer), 0);
    } while (received < 0 && errno == EINTR);

    if (received < 0) {
        std::cerr << "recv failed: " << std::strerror(errno) << "\n";
        return false;
    }
    if (received == 0) {
        std::cerr << "server closed the connection\n";
        return false;
    }
    message.assign(buffer, static_cast<size_t>(received));
    return true;
}
