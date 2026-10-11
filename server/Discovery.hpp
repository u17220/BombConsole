#ifndef DISCOVERY_HPP
#define DISCOVERY_HPP

// LAN discovery: answers "BOMBCONSOLE?" on a UDP port with "BOMBCONSOLE <tcp port> <name>",
// so that clients can find the server without typing its address.

#include <atomic>
#include <string>
#include <thread>
#include <vector>

class Discovery{
    public:
    Discovery(int udp_port, int tcp_port, const std::string &name);
    ~Discovery();
    bool ok() const { return sock >= 0; }

    // IPv4 addresses of this host (excluding loopback), for the "clients type this" hint
    static std::vector<std::string> localAddresses();

    private:
    void loop();
    int sock;
    int tcp_port;
    std::string name;
    std::atomic<bool> stopping;
    std::thread t;
};

#endif
