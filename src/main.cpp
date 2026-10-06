#include "Client.h"
#include "NetworkEmulator.h"
#include "Menu.h"
#include "Packet.h"
#include "Server.h"
#include "TrafficGenerator.h"
#include "UdpReceiver.h"
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {

void onSignal(int) { TcpServer::requestStop(); }

bool parseNumber(const char* text, const char* name, long min, long max, long& value) {
    char* end = nullptr;
    value = std::strtol(text, &end, 10);
    if (*text == '\0' || *end != '\0' || value < min || value > max) {
        std::cerr << "invalid " << name << ": " << text
                  << " (expected " << min << "-" << max << ")\n";
        return false;
    }
    return true;
}

bool parsePort(const char* text, uint16_t& port) {
    long value;
    if (!parseNumber(text, "port", 1, 65535, value)) return false;
    port = static_cast<uint16_t>(value);
    return true;
}

void printUsage(const char* program) {
    std::cerr << "usage:\n"
              << "  " << program << " server <port>\n"
              << "  " << program << " client <ip> <port> <message>\n"
              << "  " << program << " udp-recv <port> <count> <idle_ms>\n"
              << "  " << program << " udp-send <ip> <port> <count> <size> <interval_ms>\n";
}

int runServer(const char* portText) {
    uint16_t port;
    if (!parsePort(portText, port)) return 1;

    struct sigaction action{};
    action.sa_handler = onSignal;
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, nullptr);
    sigaction(SIGTERM, &action, nullptr);

    TcpServer server;
    if (!server.start(port)) return 1;
    server.run();
    return 0;
}

int runClient(const char* ip, const char* portText, const char* message) {
    uint16_t port;
    if (!parsePort(portText, port)) return 1;

    TcpClient client;
    if (!client.connectTo(ip, port)) return 1;
    if (!client.sendMessage(message)) return 1;

    std::string reply;
    if (!client.receiveMessage(reply)) return 1;
    std::cout << "echo from server: " << reply << "\n";
    return 0;
}

int runUdpReceive(const char* portText, const char* countText, const char* idleText) {
    uint16_t port;
    long count, idleMs;
    if (!parsePort(portText, port)) return 1;
    if (!parseNumber(countText, "count", 1, 1000000, count)) return 1;
    if (!parseNumber(idleText, "idle_ms", 100, 60000, idleMs)) return 1;

    UdpReceiver receiver;
    if (!receiver.open(port)) return 1;
    receiver.receive(static_cast<uint32_t>(count), static_cast<int>(idleMs));
    receiver.printReport();
    return 0;
}

int runUdpSend(char** args) {
    uint16_t port;
    long count, size, intervalMs;
    if (!parsePort(args[1], port)) return 1;
    if (!parseNumber(args[2], "count", 1, 1000000, count)) return 1;
    if (!parseNumber(args[3], "size", static_cast<long>(kHeaderSize), static_cast<long>(kMaxPacketSize), size)) return 1;
    if (!parseNumber(args[4], "interval_ms", 0, 10000, intervalMs)) return 1;

    UdpSender sender;
    if (!sender.open(args[0], port)) return 1;
    bool ok = sender.sendPackets(static_cast<uint32_t>(count), static_cast<size_t>(size),
                                 static_cast<unsigned>(intervalMs));
    std::cout << "sent " << sender.sentCount() << " packets of " << size
              << " bytes to " << args[0] << ":" << port << "\n";
    return ok ? 0 : 1;
}

}

int main(int argc, char** argv) {
    std::string mode = argc > 1 ? argv[1] : "";
    if (argc == 1 || (mode == "menu" && argc == 2)) {
        Menu menu;
        return menu.run();
    }
    if (mode == "server" && argc == 3) return runServer(argv[2]);
    if (mode == "client" && argc == 5) return runClient(argv[2], argv[3], argv[4]);
    if (mode == "udp-recv" && argc == 5) return runUdpReceive(argv[2], argv[3], argv[4]);
    if (mode == "udp-send" && argc == 7) return runUdpSend(argv + 2);
    int emulatorResult;
    if (handleEmulatorCommand(mode, argc, argv, emulatorResult)) return emulatorResult;
    printUsage(argv[0]);
    return 1;
}
