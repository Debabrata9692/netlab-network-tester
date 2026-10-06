#include "UdpReceiver.h"
#include "Packet.h"
#include <arpa/inet.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <iostream>
#include <sstream>

namespace {

constexpr int kPollSliceMs = 100;
constexpr int kStandaloneStartTimeoutMs = 60000;

int64_t steadyNowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

}

UdpReceiver::~UdpReceiver() { closeSocket(); }

void UdpReceiver::closeSocket() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

bool UdpReceiver::open(uint16_t port) {
    fd_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd_ < 0) {
        std::cerr << "socket failed: " << std::strerror(errno) << "\n";
        return false;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if (bind(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        if (errno == EADDRINUSE)
            std::cerr << "port " << port << " is already in use\n";
        else if (errno == EACCES)
            std::cerr << "permission denied for port " << port << " (ports below 1024 need root)\n";
        else
            std::cerr << "bind failed: " << std::strerror(errno) << "\n";
        closeSocket();
        return false;
    }
    std::cout << "listening on UDP port " << port << "\n";
    return true;
}

void UdpReceiver::notifySenderDone(int graceMs) {
    graceMs_ = graceMs;
    senderDoneAtMs_ = steadyNowMs();
    senderDone_ = true;
}

void UdpReceiver::handleDatagram(const uint8_t* data, size_t length) {
    uint32_t sequence;
    uint64_t sentUs;
    if (!decodeHeader(data, length, sequence, sentUs) || sequence < 1 || sequence > expected_) {
        ++malformed_;
        return;
    }
    if (seen_[sequence]) {
        ++duplicates_;
        return;
    }
    seen_[sequence] = true;
    uint64_t arrivalUs = nowMicros();
    stats_.recordReceived(static_cast<int64_t>(arrivalUs) - static_cast<int64_t>(sentUs),
                          length, arrivalUs);
    if (progress_) progress_->addReceived(static_cast<int64_t>(arrivalUs) - static_cast<int64_t>(sentUs));
}

bool UdpReceiver::waitExpired(int idleTimeoutMs, bool started, int64_t lastActivityMs,
                              int64_t nowMs) const {
    if (senderDone_) {
        int64_t reference = std::max(lastActivityMs, senderDoneAtMs_.load());
        return nowMs - reference >= graceMs_;
    }
    if (idleTimeoutMs <= 0) return false;
    int limit = started ? idleTimeoutMs : kStandaloneStartTimeoutMs;
    return nowMs - lastActivityMs >= limit;
}

void UdpReceiver::receive(uint32_t expectedCount, int idleTimeoutMs) {
    expected_ = expectedCount;
    sentLimit_ = expectedCount;
    duplicates_ = malformed_ = 0;
    stats_.reset();
    stats_.setPacketsSent(expectedCount);
    seen_.assign(expectedCount + 1, false);

    uint8_t buffer[2048];
    bool started = false;
    int64_t lastActivityMs = steadyNowMs();
    while (!stopRequested_ && stats_.packetsReceived() < expected_) {
        pollfd pfd{fd_, POLLIN, 0};
        int ready = poll(&pfd, 1, kPollSliceMs);
        if (ready < 0) {
            if (errno == EINTR) continue;
            std::cerr << "poll failed: " << std::strerror(errno) << "\n";
            break;
        }
        int64_t now = steadyNowMs();
        if (ready == 0) {
            if (waitExpired(idleTimeoutMs, started, lastActivityMs, now)) break;
            continue;
        }

        ssize_t length = recvfrom(fd_, buffer, sizeof(buffer), 0, nullptr, nullptr);
        if (length < 0) {
            if (errno == EINTR) continue;
            std::cerr << "recvfrom failed: " << std::strerror(errno) << "\n";
            break;
        }
        started = true;
        lastActivityMs = now;
        handleDatagram(buffer, static_cast<size_t>(length));
    }
    closeSocket();
}

std::string UdpReceiver::report() const {
    std::ostringstream out;
    out << stats_.summary();
    if (duplicates_) out << "Duplicates: " << duplicates_ << "\n";
    if (malformed_) out << "Malformed/out-of-range: " << malformed_ << "\n";
    if (stats_.packetsLost() == 0) return out.str();

    out << "Missing:";
    int shown = 0;
    uint32_t sequence = 1;
    while (sequence <= sentLimit_) {
        if (seen_[sequence]) {
            ++sequence;
            continue;
        }
        uint32_t start = sequence;
        while (sequence <= sentLimit_ && !seen_[sequence]) ++sequence;
        if (shown == 20) {
            out << " ...";
            break;
        }
        if (start == sequence - 1)
            out << " " << start;
        else
            out << " " << start << "-" << sequence - 1;
        ++shown;
    }
    out << "\n";
    return out.str();
}

void UdpReceiver::printReport() const { std::cout << report(); }
