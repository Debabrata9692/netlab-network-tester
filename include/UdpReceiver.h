#pragma once
#include "LiveProgress.h"
#include "Statistics.h"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class UdpReceiver {
public:
    UdpReceiver() = default;
    ~UdpReceiver();
    UdpReceiver(const UdpReceiver&) = delete;
    UdpReceiver& operator=(const UdpReceiver&) = delete;

    bool open(uint16_t port);
    void receive(uint32_t expectedCount, int idleTimeoutMs);
    void requestStop() { stopRequested_ = true; }
    void notifySenderDone(int graceMs);
    void setSentCount(uint32_t sent) { stats_.setPacketsSent(sent); sentLimit_ = sent; }
    void setProgress(LiveProgress* progress) { progress_ = progress; }
    std::string report() const;
    void printReport() const;
    const Statistics& stats() const { return stats_; }

private:
    void closeSocket();
    void handleDatagram(const uint8_t* data, size_t length);
    bool waitExpired(int idleTimeoutMs, bool started, int64_t lastActivityMs, int64_t nowMs) const;

    int fd_ = -1;
    uint32_t expected_ = 0;
    uint32_t duplicates_ = 0;
    uint32_t malformed_ = 0;
    Statistics stats_;
    std::vector<bool> seen_;
    uint32_t sentLimit_ = 0;
    LiveProgress* progress_ = nullptr;
    std::atomic<bool> stopRequested_{false};
    std::atomic<bool> senderDone_{false};
    std::atomic<int> graceMs_{0};
    std::atomic<int64_t> senderDoneAtMs_{0};
};
