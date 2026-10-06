#include "Statistics.h"
#include <algorithm>
#include <iomanip>
#include <sstream>

void Statistics::reset() { *this = Statistics(); }

void Statistics::setPacketsSent(uint32_t count) { sent_ = count; }

void Statistics::recordReceived(int64_t latencyUs, size_t bytes, uint64_t arrivalUs) {
    if (received_ == 0)
        firstArrivalUs_ = arrivalUs;
    else
        bytesAfterFirst_ += bytes;
    lastArrivalUs_ = std::max(lastArrivalUs_, arrivalUs);
    ++received_;
    minUs_ = std::min(minUs_, latencyUs);
    maxUs_ = std::max(maxUs_, latencyUs);
    sumUs_ += latencyUs;
}

uint32_t Statistics::packetsLost() const {
    return sent_ > received_ ? sent_ - received_ : 0;
}

double Statistics::lossPercent() const {
    return sent_ ? 100.0 * packetsLost() / sent_ : 0.0;
}

double Statistics::minLatencyMs() const {
    return received_ ? static_cast<double>(minUs_) / 1000.0 : 0.0;
}

double Statistics::maxLatencyMs() const {
    return received_ ? static_cast<double>(maxUs_) / 1000.0 : 0.0;
}

double Statistics::avgLatencyMs() const {
    return received_ ? static_cast<double>(sumUs_) / received_ / 1000.0 : 0.0;
}

double Statistics::durationSeconds() const {
    if (received_ < 2) return 0.0;
    return static_cast<double>(lastArrivalUs_ - firstArrivalUs_) / 1e6;
}

double Statistics::throughputKbps() const {
    double seconds = durationSeconds();
    if (seconds <= 0.0) return 0.0;
    return static_cast<double>(bytesAfterFirst_) * 8.0 / seconds / 1000.0;
}

std::string Statistics::summary() const {
    std::ostringstream out;
    out << std::fixed << std::setprecision(2);
    out << "Packets Sent: " << sent_ << "\n"
        << "Packets Received: " << received_ << "\n"
        << "Packets Lost: " << packetsLost() << "\n"
        << "Packet Loss: " << lossPercent() << "%\n";
    if (received_ == 0) {
        out << "Latency: n/a (no packets received)\n";
    } else {
        out << "Minimum " << latencyLabel_ << ": " << minLatencyMs() << " ms\n"
            << "Maximum " << latencyLabel_ << ": " << maxLatencyMs() << " ms\n"
            << "Average " << latencyLabel_ << ": " << avgLatencyMs() << " ms\n";
    }
    if (received_ < 2) {
        out << "Test Duration: n/a\nThroughput: n/a\n";
    } else {
        out << "Test Duration: " << durationSeconds() << " s\n"
            << std::setprecision(1) << "Throughput: " << throughputKbps() << " kbit/s\n";
    }
    return out.str();
}
