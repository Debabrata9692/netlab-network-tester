#include "TrafficGenerator.h"
#include "Packet.h"
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>

UdpSender::~UdpSender() {
    if (fd_ >= 0) close(fd_);
}

bool UdpSender::open(const std::string& ip, uint16_t port) {
    dest_.sin_family = AF_INET;
    dest_.sin_port = htons(port);
    if (inet_pton(AF_INET, ip.c_str(), &dest_.sin_addr) != 1) {
        std::cerr << "invalid IP address: " << ip << "\n";
        return false;
    }
    fd_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd_ < 0) {
        std::cerr << "socket failed: " << std::strerror(errno) << "\n";
        return false;
    }
    return true;
}

bool UdpSender::sendPackets(uint32_t count, size_t packetSize, unsigned intervalMs,
                            unsigned maxDurationMs) {
    std::vector<uint8_t> buffer(packetSize, 0);
    const auto started = std::chrono::steady_clock::now();
    for (uint32_t sequence = 1; sequence <= count; ++sequence) {
        if (maxDurationMs > 0 &&
            std::chrono::steady_clock::now() - started >= std::chrono::milliseconds(maxDurationMs))
            break;
        encodeHeader(buffer.data(), sequence, nowMicros());
        ssize_t result;
        do {
            result = sendto(fd_, buffer.data(), buffer.size(), 0,
                            reinterpret_cast<const sockaddr*>(&dest_), sizeof(dest_));
        } while (result < 0 && errno == EINTR);
        if (result < 0) {
            std::cerr << "sendto failed: " << std::strerror(errno) << "\n";
            return false;
        }
        ++sent_;
        if (progress_) progress_->setSent(sent_);
        if (intervalMs > 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(intervalMs));
    }
    return true;
}
