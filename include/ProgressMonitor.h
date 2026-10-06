#pragma once
#include "LiveProgress.h"
#include <condition_variable>
#include <mutex>
#include <thread>

class ProgressMonitor {
public:
    explicit ProgressMonitor(const LiveProgress& progress);
    ~ProgressMonitor();
    ProgressMonitor(const ProgressMonitor&) = delete;
    ProgressMonitor& operator=(const ProgressMonitor&) = delete;

    void stop();

private:
    void run();
    void printLine(const LiveProgress::Snapshot& snapshot);

    const LiveProgress& progress_;
    std::mutex mutex_;
    std::condition_variable wake_;
    bool stopRequested_ = false;
    bool printed_ = false;
    std::thread thread_;
};
