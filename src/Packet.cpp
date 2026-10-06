#include "Packet.h"
#include <arpa/inet.h>
#include <chrono>
#include <cstring>

void encodeHeader(uint8_t* buffer, uint32_t sequence, uint64_t timestampUs) {
    uint32_t seqNet = htonl(sequence);
    uint32_t highNet = htonl(static_cast<uint32_t>(timestampUs >> 32));
    uint32_t lowNet = htonl(static_cast<uint32_t>(timestampUs & 0xFFFFFFFFu));
    std::memcpy(buffer, &seqNet, 4);
    std::memcpy(buffer + 4, &highNet, 4);
    std::memcpy(buffer + 8, &lowNet, 4);
}

bool decodeHeader(const uint8_t* buffer, size_t length, uint32_t& sequence, uint64_t& timestampUs) {
    if (length < kHeaderSize) return false;
    uint32_t seqNet, highNet, lowNet;
    std::memcpy(&seqNet, buffer, 4);
    std::memcpy(&highNet, buffer + 4, 4);
    std::memcpy(&lowNet, buffer + 8, 4);
    sequence = ntohl(seqNet);
    timestampUs = (static_cast<uint64_t>(ntohl(highNet)) << 32) | ntohl(lowNet);
    return true;
}

uint64_t nowMicros() {
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(now).count());
}
