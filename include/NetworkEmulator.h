#pragma once
#include <cstdint>
#include <string>
#include <vector>

class NetworkEmulator {
public:
    static constexpr const char* kNamespace = "chaosns";
    static constexpr const char* kHostIf = "chaos0";
    static constexpr const char* kPeerIf = "chaos1";
    static constexpr const char* kDefaultPeerIp = "10.200.0.2";

    bool setup(const std::string& peerIp = kDefaultPeerIp);
    bool teardown();
    bool setDelay(int delayMs);
    bool setImpairment(int delayMs, double lossPercent);
    bool setFromText(const char* delayText, const char* lossText);
    bool clear();
    bool show();
    bool interfaceExists() const;

    struct KernelCounters {
        bool netemActive = false;
        uint64_t sentPackets = 0;
        uint64_t droppedPackets = 0;
        uint64_t txPackets = 0;
    };
    bool readKernelCounters(KernelCounters& counters) const;
    static std::string formatKernelReport(const KernelCounters& before, const KernelCounters& after,
                                          uint32_t appLostPackets, bool reliableProtocol);

    static bool validatePeerIp(const std::string& ip, std::string& error);

private:
    static bool requireRoot();
    static bool run(const std::vector<std::string>& args, bool quiet = false);
    static bool runCapture(const std::vector<std::string>& args, std::string& output);
};

bool handleEmulatorCommand(const std::string& mode, int argc, char** argv, int& result);
