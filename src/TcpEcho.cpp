#include "TcpEcho.h"
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <iostream>

namespace {

constexpr int kPollSliceMs = 100;

void enableNoDelay(int fd) {
    int enable = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &enable, sizeof(enable));
}

}

TcpEchoServer::~TcpEchoServer() { closeListener(); }

void TcpEchoServer::closeListener() {
    if (listenFd_ >= 0) {
        close(listenFd_);
        listenFd_ = -1;
    }
}

bool TcpEchoServer::open(uint16_t port) {
    listenFd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd_ < 0) {
        std::cerr << "socket failed: " << std::strerror(errno) << "\n";
        return false;
    }

    int enable = 1;
    setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if (bind(listenFd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        if (errno == EADDRINUSE)
            std::cerr << "port " << port << " is already in use\n";
        else if (errno == EACCES)
            std::cerr << "permission denied for port " << port << " (ports below 1024 need root)\n";
        else
            std::cerr << "bind failed: " << std::strerror(errno) << "\n";
        closeListener();
        return false;
    }
    if (listen(listenFd_, 1) < 0) {
        std::cerr << "listen failed: " << std::strerror(errno) << "\n";
        closeListener();
        return false;
    }
    std::cout << "listening on TCP port " << port << "\n";
    return true;
}

void TcpEchoServer::serve() {
    while (!stopRequested_) {
        pollfd pfd{listenFd_, POLLIN, 0};
        int ready = poll(&pfd, 1, kPollSliceMs);
        if (ready < 0) {
            if (errno == EINTR) continue;
            std::cerr << "poll failed: " << std::strerror(errno) << "\n";
            break;
        }
        if (ready == 0) continue;

        int clientFd = accept(listenFd_, nullptr, nullptr);
        if (clientFd < 0) {
            if (errno != EINTR) std::cerr << "accept failed: " << std::strerror(errno) << "\n";
            continue;
        }
        enableNoDelay(clientFd);
        echoClient(clientFd);
        close(clientFd);
        break;
    }
    closeListener();
}

void TcpEchoServer::echoClient(int clientFd) {
    char buffer[4096];
    while (!stopRequested_) {
        pollfd pfd{clientFd, POLLIN, 0};
        int ready = poll(&pfd, 1, kPollSliceMs);
        if (ready < 0) {
            if (errno == EINTR) continue;
            return;
        }
        if (ready == 0) continue;

        ssize_t received = recv(clientFd, buffer, sizeof(buffer), 0);
        if (received < 0) {
            if (errno == EINTR) continue;
            return;
        }
        if (received == 0) return;

        size_t total = 0;
        while (total < static_cast<size_t>(received)) {
            ssize_t sent = send(clientFd, buffer + total, static_cast<size_t>(received) - total, MSG_NOSIGNAL);
            if (sent < 0) {
                if (errno == EINTR) continue;
                return;
            }
            total += static_cast<size_t>(sent);
        }
    }
}

TcpEchoClient::~TcpEchoClient() { disconnect(); }

void TcpEchoClient::disconnect() {
    if (fd_ >= 0) {
        close(fd_);
        fd_ = -1;
    }
}

bool TcpEchoClient::connectTo(const std::string& ip, uint16_t port, int timeoutMs) {
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

    int flags = fcntl(fd_, F_GETFL, 0);
    fcntl(fd_, F_SETFL, flags | O_NONBLOCK);

    int result = connect(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    if (result < 0 && errno != EINPROGRESS) {
        if (errno == ECONNREFUSED)
            std::cerr << "server unavailable at " << ip << ":" << port << "\n";
        else
            std::cerr << "connect failed: " << std::strerror(errno) << "\n";
        disconnect();
        return false;
    }

    if (result < 0) {
        pollfd pfd{fd_, POLLOUT, 0};
        int ready;
        do {
            ready = poll(&pfd, 1, timeoutMs);
        } while (ready < 0 && errno == EINTR);
        if (ready <= 0) {
            std::cerr << (ready == 0 ? "connection timed out" : "poll failed") << "\n";
            disconnect();
            return false;
        }
        int socketError = 0;
        socklen_t length = sizeof(socketError);
        getsockopt(fd_, SOL_SOCKET, SO_ERROR, &socketError, &length);
        if (socketError != 0) {
            if (socketError == ECONNREFUSED)
                std::cerr << "server unavailable at " << ip << ":" << port << "\n";
            else
                std::cerr << "connect failed: " << std::strerror(socketError) << "\n";
            disconnect();
            return false;
        }
    }

    fcntl(fd_, F_SETFL, flags);
    enableNoDelay(fd_);
    return true;
}

bool TcpEchoClient::exchange(const uint8_t* data, size_t length, uint8_t* reply, int timeoutMs) {
    size_t sent = 0;
    while (sent < length) {
        ssize_t result = send(fd_, data + sent, length - sent, MSG_NOSIGNAL);
        if (result < 0) {
            if (errno == EINTR) continue;
            std::cerr << "send failed: " << std::strerror(errno) << "\n";
            return false;
        }
        sent += static_cast<size_t>(result);
    }

    size_t received = 0;
    while (received < length) {
        pollfd pfd{fd_, POLLIN, 0};
        int ready = poll(&pfd, 1, timeoutMs);
        if (ready < 0) {
            if (errno == EINTR) continue;
            std::cerr << "poll failed: " << std::strerror(errno) << "\n";
            return false;
        }
        if (ready == 0) {
            std::cerr << "timed out waiting for the echo\n";
            return false;
        }
        ssize_t result = recv(fd_, reply + received, length - received, 0);
        if (result < 0) {
            if (errno == EINTR) continue;
            std::cerr << "recv failed: " << std::strerror(errno) << "\n";
            return false;
        }
        if (result == 0) {
            std::cerr << "server closed the connection\n";
            return false;
        }
        received += static_cast<size_t>(result);
    }
    return true;
}
