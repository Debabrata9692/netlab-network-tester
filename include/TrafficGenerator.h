#pragma once
#include <cstddef>
#include <cstdint>
#include "LiveProgress.h"
#include <netinet/in.h>
#include <string>

class UdpSender {
public:
    UdpSender() = default;
    ~UdpSender();
    UdpSender(const UdpSender&) = delete;
    UdpSender& operator=(const UdpSender&) = delete;

    bool open(const std::string& ip, uint16_t port);
    bool sendPackets(uint32_t count, size_t packetSize, unsigned intervalMs, unsigned maxDurationMs = 0);
    uint32_t sentCount() const { return sent_; }
    void setProgress(LiveProgress* progress) { progress_ = progress; }

private:
    int fd_ = -1;
    sockaddr_in dest_{};
    uint32_t sent_ = 0;
    LiveProgress* progress_ = nullptr;
};
