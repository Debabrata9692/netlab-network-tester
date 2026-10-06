#pragma once
#include "Packet.h"
#include <cstdint>
#include <string>

enum class Protocol { Udp, Tcp };

inline const char* protocolName(Protocol protocol) {
    return protocol == Protocol::Udp ? "UDP" : "TCP";
}

struct NetworkConfig {
    static constexpr long kMaxLatencyMs = 10000;
    static constexpr long kMaxPacketCount = 1000000;
    static constexpr long kMaxIntervalMs = 10000;
    static constexpr long kMaxDurationSec = 3600;
    static constexpr long kMinSize = static_cast<long>(kHeaderSize);
    static constexpr long kMaxSize = static_cast<long>(kMaxPacketSize);

    Protocol protocol = Protocol::Udp;
    int latencyMs = 0;
    double lossPercent = 0.0;
    uint32_t packetCount = 100;
    uint32_t packetSize = 200;
    uint32_t intervalMs = 10;
    uint32_t durationSec = 0;
    uint16_t port = 9001;
    std::string serverIp = "10.200.0.2";

    void reset() { *this = NetworkConfig(); }
    std::string toString() const;
};

std::string trimWhitespace(const std::string& text);
bool parseLong(const std::string& text, long min, long max, long& value);
bool parseDouble(const std::string& text, double min, double max, double& value);
