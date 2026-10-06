#pragma once
#include <cstdint>
#include <mutex>

class LiveProgress {
public:
    struct Snapshot {
        uint32_t planned = 0;
        uint32_t sent = 0;
        uint32_t received = 0;
        double lastLatencyMs = 0.0;
        double avgLatencyMs = 0.0;
    };

    void reset(uint32_t planned) {
        std::lock_guard<std::mutex> lock(mutex_);
        planned_ = planned;
        sent_ = 0;
        received_ = 0;
        lastUs_ = 0;
        sumUs_ = 0;
    }

    void setSent(uint32_t sent) {
        std::lock_guard<std::mutex> lock(mutex_);
        sent_ = sent;
    }

    void addReceived(int64_t latencyUs) {
        std::lock_guard<std::mutex> lock(mutex_);
        ++received_;
        lastUs_ = latencyUs;
        sumUs_ += latencyUs;
    }

    Snapshot snapshot() const {
        std::lock_guard<std::mutex> lock(mutex_);
        Snapshot result;
        result.planned = planned_;
        result.sent = sent_;
        result.received = received_;
        result.lastLatencyMs = static_cast<double>(lastUs_) / 1000.0;
        result.avgLatencyMs =
            received_ ? static_cast<double>(sumUs_) / received_ / 1000.0 : 0.0;
        return result;
    }

private:
    mutable std::mutex mutex_;
    uint32_t planned_ = 0;
    uint32_t sent_ = 0;
    uint32_t received_ = 0;
    int64_t lastUs_ = 0;
    int64_t sumUs_ = 0;
};
