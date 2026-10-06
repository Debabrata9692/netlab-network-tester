#pragma once
#include <cstddef>
#include <cstdint>

constexpr size_t kHeaderSize = 12;
constexpr size_t kMaxPacketSize = 1472;

void encodeHeader(uint8_t* buffer, uint32_t sequence, uint64_t timestampUs);
bool decodeHeader(const uint8_t* buffer, size_t length, uint32_t& sequence, uint64_t& timestampUs);
uint64_t nowMicros();
