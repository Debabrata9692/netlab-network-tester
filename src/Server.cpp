#include "Server.h"
#include <arpa/inet.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <string>

std::atomic<bool> TcpServer::stopRequested_{false};

TcpServer::~TcpServer() { stop(); }

void TcpServer::requestStop() { stopRequested_ = true; }

void TcpServer::stop() {
    if (listenFd_ >= 0) {
        close(listenFd_);
        listenFd_ = -1;
    }
}

bool TcpServer::start(uint16_t port) {
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
        stop();
        return false;
    }

    if (listen(listenFd_, 8) < 0) {
        std::cerr << "listen failed: " << std::strerror(errno) << "\n";
        stop();
        return false;
    }

    std::cout << "server listening on port " << port << "\n";
    return true;
}

void TcpServer::run() {
    while (!stopRequested_) {
        pollfd pfd{listenFd_, POLLIN, 0};
        int ready = poll(&pfd, 1, 500);
        if (ready < 0) {
            if (errno == EINTR) continue;
            std::cerr << "poll failed: " << std::strerror(errno) << "\n";
            break;
        }
        if (ready == 0) continue;

        sockaddr_in peer{};
        socklen_t peerLen = sizeof(peer);
        int clientFd = accept(listenFd_, reinterpret_cast<sockaddr*>(&peer), &peerLen);
        if (clientFd < 0) {
            if (errno != EINTR)
                std::cerr << "accept failed: " << std::strerror(errno) << "\n";
            continue;
        }
        handleClient(clientFd, peer);
        close(clientFd);
    }
    std::cout << "server stopped\n";
}

void TcpServer::handleClient(int clientFd, const sockaddr_in& peer) {
    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &peer.sin_addr, ip, sizeof(ip));
    std::cout << "client connected: " << ip << ":" << ntohs(peer.sin_port) << "\n";

    char buffer[1024];
    while (true) {
        ssize_t received = recv(clientFd, buffer, sizeof(buffer), 0);
        if (received < 0) {
            if (errno == EINTR) continue;
            std::cerr << "recv failed: " << std::strerror(errno) << "\n";
            break;
        }
        if (received == 0) break;

        std::cout << "received " << received << " bytes: "
                  << std::string(buffer, static_cast<size_t>(received)) << "\n";

        ssize_t sent = send(clientFd, buffer, static_cast<size_t>(received), MSG_NOSIGNAL);
        if (sent < 0) {
            std::cerr << "send failed: " << std::strerror(errno) << "\n";
            break;
        }
    }
    std::cout << "client disconnected\n";
}
