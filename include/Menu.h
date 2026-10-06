#pragma once
#include "Logger.h"
#include "NetworkConfig.h"
#include "NetworkEmulator.h"
#include "TestRunner.h"
#include <string>

class Menu {
public:
    Menu() = default;
    ~Menu();
    Menu(const Menu&) = delete;
    Menu& operator=(const Menu&) = delete;

    int run();

private:
    void printMenu() const;
    void printConfigureMenu() const;
    void handleChoice(int choice);
    void configureMenu();
    void resetConfiguration();
    void notAvailable(const char* feature) const;

    void startServer();
    void startTest();
    void displayStatistics() const;
    void saveResults();
    void stopServerAfterChange();

    bool readLine(std::string& line);
    bool promptLong(const std::string& label, long min, long max, long& value);
    bool promptDouble(const std::string& label, double min, double max, double& value);
    bool applyNetem();

    void editLatency();
    void editLoss();
    void editPacketCount();
    void editPacketSize();
    void editInterval();
    void editPort();
    void editProtocol();
    void editDuration();
    void editServerIp();

    NetworkConfig config_;
    NetworkEmulator emulator_;
    TestRunner runner_;
    TestResult lastResult_;
    bool haveResult_ = false;
    std::string savedPath_;
    std::string kernelReport_;
    Logger logger_;
    bool networkUp_ = false;
    bool exitRequested_ = false;
    bool inputClosed_ = false;
};
