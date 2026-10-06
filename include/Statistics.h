#pragma once
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>

class Statistics {
public:
    void reset();
    void setPacketsSent(uint32_t count);
    void setLatencyLabel(const std::string& label) { latencyLabel_ = label; }
    void recordReceived(int64_t latencyUs, size_t bytes, uint64_t arrivalUs);

    uint32_t packetsSent() const { return sent_; }
    uint32_t packetsReceived() const { return received_; }
    uint32_t packetsLost() const;
    double lossPercent() const;
    double minLatencyMs() const;
    double maxLatencyMs() const;
    double avgLatencyMs() const;
    double durationSeconds() const;
    double throughputKbps() const;
    std::string summary() const;

private:
    std::string latencyLabel_ = "Latency";
    uint32_t sent_ = 0;
    uint32_t received_ = 0;
    uint64_t bytesAfterFirst_ = 0;
    int64_t minUs_ = std::numeric_limits<int64_t>::max();
    int64_t maxUs_ = std::numeric_limits<int64_t>::min();
    int64_t sumUs_ = 0;
    uint64_t firstArrivalUs_ = 0;
    uint64_t lastArrivalUs_ = 0;
};
