#include "ProgressMonitor.h"
#include <sys/ioctl.h>
#include <unistd.h>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <string>

namespace {

constexpr auto kRefreshInterval = std::chrono::milliseconds(250);

}

ProgressMonitor::ProgressMonitor(const LiveProgress& progress) : progress_(progress) {
    if (isatty(STDOUT_FILENO)) thread_ = std::thread(&ProgressMonitor::run, this);
}

ProgressMonitor::~ProgressMonitor() { stop(); }

void ProgressMonitor::stop() {
    if (!thread_.joinable()) return;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopRequested_ = true;
    }
    wake_.notify_all();
    thread_.join();
    if (printed_) {
        std::cout << "\n";
        printed_ = false;
    }
}

void ProgressMonitor::run() {
    std::unique_lock<std::mutex> lock(mutex_);
    while (!wake_.wait_for(lock, kRefreshInterval, [this] { return stopRequested_; })) {
        lock.unlock();
        printLine(progress_.snapshot());
        lock.lock();
    }
}

void ProgressMonitor::printLine(const LiveProgress::Snapshot& snapshot) {
    unsigned outstanding = snapshot.sent >= snapshot.received ? snapshot.sent - snapshot.received : 0u;
    char line[160];
    std::snprintf(line, sizeof(line),
                  "[live] sent %u/%u | recv %u | out %u | last %.1f ms | avg %.1f ms",
                  snapshot.sent, snapshot.planned, snapshot.received, outstanding,
                  snapshot.lastLatencyMs, snapshot.avgLatencyMs);

    std::string text = line;
    winsize size{};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0 && size.ws_col > 1 &&
        text.size() >= static_cast<size_t>(size.ws_col))
        text.resize(static_cast<size_t>(size.ws_col) - 1);

    std::cout << "\r\033[K" << text << std::flush;
    printed_ = true;
}
