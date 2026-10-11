#include "Discovery.hpp"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

Discovery::Discovery(int udp_port, int tcp_port_, const std::string &name_):
    sock(-1), tcp_port(tcp_port_), name(name_), stopping(false)
{
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if(sock < 0)
        return;
    int yes = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
#ifdef SO_REUSEPORT
    setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(yes));
#endif
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons((uint16_t)udp_port);
    if(bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0){
        close(sock);
        sock = -1;
        return;
    }
    t = std::thread(&Discovery::loop, this);
}

Discovery::~Discovery(){
    stopping = true;
    if(sock >= 0){
        t.join();
        close(sock);
    }
}

void Discovery::loop(){
    while(!stopping){
        struct pollfd pfd;
        pfd.fd = sock;
        pfd.events = POLLIN;
        pfd.revents = 0;
        if(poll(&pfd, 1, 200) <= 0)
            continue;
        char buf[128];
        struct sockaddr_in from;
        socklen_t flen = sizeof(from);
        ssize_t n = recvfrom(sock, buf, sizeof(buf) - 1, 0, (struct sockaddr *)&from, &flen);
        if(n <= 0)
            continue;
        buf[n] = '\0';
        if(strncmp(buf, "BOMBCONSOLE?", 12) != 0)
            continue;
        std::string reply = "BOMBCONSOLE " + std::to_string(tcp_port) + " " + name;
        sendto(sock, reply.c_str(), reply.size(), 0, (struct sockaddr *)&from, flen);
    }
}

std::vector<std::string> Discovery::localAddresses(){
    std::vector<std::string> out;
    struct ifaddrs *ifs = nullptr;
    if(getifaddrs(&ifs) != 0)
        return out;
    for(struct ifaddrs *i = ifs; i != nullptr; i = i->ifa_next){
        if(i->ifa_addr == nullptr || i->ifa_addr->sa_family != AF_INET)
            continue;
        if(!(i->ifa_flags & IFF_UP) || (i->ifa_flags & IFF_LOOPBACK))
            continue;
        char ip[INET_ADDRSTRLEN];
        struct sockaddr_in *sa = (struct sockaddr_in *)i->ifa_addr;
        if(inet_ntop(AF_INET, &sa->sin_addr, ip, sizeof(ip)) != nullptr)
            out.push_back(ip);
    }
    freeifaddrs(ifs);
    return out;
}
