#include "NetworkConfig.h"
#include <cerrno>
#include <cstdlib>
#include <sstream>

std::string trimWhitespace(const std::string& text) {
    const char* whitespace = " \t\r\n";
    size_t first = text.find_first_not_of(whitespace);
    if (first == std::string::npos) return "";
    size_t last = text.find_last_not_of(whitespace);
    return text.substr(first, last - first + 1);
}

bool parseLong(const std::string& text, long min, long max, long& value) {
    std::string trimmed = trimWhitespace(text);
    if (trimmed.empty()) return false;
    errno = 0;
    char* end = nullptr;
    long parsed = std::strtol(trimmed.c_str(), &end, 10);
    if (errno == ERANGE || *end != '\0' || parsed < min || parsed > max) return false;
    value = parsed;
    return true;
}

bool parseDouble(const std::string& text, double min, double max, double& value) {
    std::string trimmed = trimWhitespace(text);
    if (trimmed.empty()) return false;
    if (trimmed.find_first_not_of("0123456789.") != std::string::npos) return false;
    errno = 0;
    char* end = nullptr;
    double parsed = std::strtod(trimmed.c_str(), &end);
    if (errno == ERANGE || *end != '\0' || !(parsed >= min && parsed <= max)) return false;
    value = parsed;
    return true;
}

std::string NetworkConfig::toString() const {
    std::ostringstream out;
    out << "  Protocol:      " << protocolName(protocol) << "\n"
        << "  Latency:       " << latencyMs << " ms\n"
        << "  Packet loss:   " << lossPercent << " %\n"
        << "  Packet count:  " << packetCount << "\n"
        << "  Packet size:   " << packetSize << " bytes\n"
        << "  Interval:      " << intervalMs << " ms\n";
    if (durationSec == 0)
        out << "  Time limit:    none\n";
    else
        out << "  Time limit:    " << durationSec << " s\n";
    out << "  Server IP:     " << serverIp << "\n"
        << "  Port:          " << port << "\n";
    return out.str();
}
