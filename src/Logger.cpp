#include "Logger.h"
#include <fcntl.h>
#include <unistd.h>
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace fs = std::filesystem;

namespace {

int highestExistingId(const fs::path& directory) {
    int highest = 0;
    std::error_code error;
    for (const auto& entry : fs::directory_iterator(directory, error)) {
        std::string name = entry.path().filename().string();
        if (name.size() <= 9 || name.compare(0, 5, "test_") != 0 ||
            name.compare(name.size() - 4, 4, ".txt") != 0)
            continue;
        std::string digits = name.substr(5, name.size() - 9);
        if (digits.size() > 9 || digits.find_first_not_of("0123456789") != std::string::npos)
            continue;
        highest = std::max(highest, std::atoi(digits.c_str()));
    }
    return highest;
}

std::string timestamp() {
    std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_r(&now, &local);
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &local);
    return buffer;
}

std::string formatResult(int id, const NetworkConfig& config, const Statistics& stats) {
    const char* label = config.protocol == Protocol::Tcp ? "RTT" : "Latency";
    char idText[16];
    std::snprintf(idText, sizeof(idText), "%03d", id);

    std::ostringstream out;
    out << std::fixed << std::setprecision(2);
    out << "Test ID: " << idText << "\n"
        << "Date: " << timestamp() << "\n"
        << "Protocol: " << protocolName(config.protocol) << "\n"
        << "Configured Latency: " << config.latencyMs << " ms\n"
        << "Configured Packet Loss: " << config.lossPercent << "%\n"
        << "Packet Size: " << config.packetSize << " bytes\n"
        << "Send Interval: " << config.intervalMs << " ms\n"
        << "Packets Sent: " << stats.packetsSent() << "\n"
        << "Packets Received: " << stats.packetsReceived() << "\n"
        << "Packets Lost: " << stats.packetsLost() << "\n"
        << "Packet Loss: " << stats.lossPercent() << "%\n";

    if (stats.packetsReceived() == 0) {
        out << "Average " << label << ": n/a\n"
            << "Minimum " << label << ": n/a\n"
            << "Maximum " << label << ": n/a\n";
    } else {
        out << "Average " << label << ": " << stats.avgLatencyMs() << " ms\n"
            << "Minimum " << label << ": " << stats.minLatencyMs() << " ms\n"
            << "Maximum " << label << ": " << stats.maxLatencyMs() << " ms\n";
    }

    if (stats.packetsReceived() < 2) {
        out << "Test Duration: n/a\nThroughput: n/a\n";
    } else {
        out << "Test Duration: " << stats.durationSeconds() << " s\n"
            << std::setprecision(1) << "Throughput: " << stats.throughputKbps() << " kbit/s\n";
    }
    return out.str();
}

bool writeAll(int fd, const std::string& text) {
    size_t total = 0;
    while (total < text.size()) {
        ssize_t written = write(fd, text.data() + total, text.size() - total);
        if (written < 0) {
            if (errno == EINTR) continue;
            std::cerr << "write failed: " << std::strerror(errno) << "\n";
            return false;
        }
        total += static_cast<size_t>(written);
    }
    return true;
}

void giveToSudoUser(int fd) {
    const char* uidText = std::getenv("SUDO_UID");
    const char* gidText = std::getenv("SUDO_GID");
    if (!uidText || !gidText) return;
    char* uidEnd = nullptr;
    char* gidEnd = nullptr;
    long uid = std::strtol(uidText, &uidEnd, 10);
    long gid = std::strtol(gidText, &gidEnd, 10);
    if (*uidEnd != '\0' || *gidEnd != '\0') return;
    if (fchown(fd, static_cast<uid_t>(uid), static_cast<gid_t>(gid)) != 0)
        std::cerr << "note: could not change the file owner: " << std::strerror(errno) << "\n";
}

}

bool Logger::save(const NetworkConfig& config, const Statistics& stats, std::string& savedPath,
                  const std::string& extraText) const {
    std::error_code error;
    fs::create_directories(directory_, error);
    if (error) {
        std::cerr << "cannot create results directory '" << directory_ << "': " << error.message() << "\n";
        return false;
    }

    int id = highestExistingId(directory_) + 1;
    for (int attempt = 0; attempt < 1000; ++attempt, ++id) {
        char name[32];
        std::snprintf(name, sizeof(name), "test_%03d.txt", id);
        std::string path = (fs::path(directory_) / name).string();

        int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
        if (fd < 0) {
            if (errno == EEXIST) continue;
            std::cerr << "cannot create " << path << ": " << std::strerror(errno) << "\n";
            return false;
        }

        bool ok = writeAll(fd, formatResult(id, config, stats) + extraText);
        giveToSudoUser(fd);
        if (close(fd) != 0) ok = false;
        if (!ok) {
            unlink(path.c_str());
            return false;
        }
        savedPath = path;
        return true;
    }
    std::cerr << "could not find a free test id in '" << directory_ << "'\n";
    return false;
}
