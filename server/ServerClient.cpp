#include "ServerClient.hpp"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

static const int ACCEPT_POLL_MS = 100;
static const size_t MAX_LINE = 1024;

Client::Client(Server *server_, int ss_, const std::string &addr_):
    server(server_), ss(ss_), addr(addr_), t(nullptr), connected(true)
{
}

Client::~Client(){
    shutdown(ss, SHUT_RDWR);
    if(t != nullptr){
        t->join();
        delete t;
    }
    close(ss);
}

void Client::start(){
    t = new std::thread(&Client::task, this);
}

bool Client::send(const std::string &data){
    std::lock_guard<std::mutex> lock(send_mtx);
    if(!connected)
        return false;
    size_t sent = 0;
    while(sent < data.size()){
        ssize_t r = ::send(ss, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
        if(r < 0){
            if(errno == EINTR)
                continue;
            // Timeout or broken pipe: the reader thread reports the disconnection
            return false;
        }
        sent += (size_t)r;
    }
    return true;
}

void Client::task(){
    std::string buf;
    char tmp[512];
    while(true){
        ssize_t len = read(ss, tmp, sizeof(tmp));
        if(len > 0){
            buf.append(tmp, (size_t)len);
            size_t pos;
            while((pos = buf.find('\n')) != std::string::npos){
                std::string line = buf.substr(0, pos);
                buf.erase(0, pos + 1);
                if(!line.empty() && line.back() == '\r')
                    line.pop_back();
                NetEvent e;
                e.type = NetEvent::LINE;
                e.client = this;
                e.line = line;
                server->pushEvent(e);
            }
            if(buf.size() > MAX_LINE)
                break;      // garbage without newline
        }
        else if(len == 0){
            break;          // closed by the peer
        }
        else if(errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR){
            continue;       // receive timeout: keep waiting
        }
        else{
            break;          // socket error (or shutdown by the destructor)
        }
    }
    connected = false;
    NetEvent e;
    e.type = NetEvent::DISCONNECT;
    e.client = this;
    server->pushEvent(e);
}

//

Server::Server(uint16_t port):
    s(-1), bound_port(0), stopping(false)
{
    s = socket(AF_INET, SOCK_STREAM, 0);
    if(s < 0){
        perror("ERROR on socket");
        exit(1);
    }
    int yes = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));   // sin_len / sin_zero must be zero on BSD / macOS
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    if(bind(s, (struct sockaddr *)&addr, sizeof(addr))){
        perror("ERROR on bind");
        exit(1);
    }
    if(listen(s, 64)){
        perror("ERROR on listen");
        exit(1);
    }
    socklen_t alen = sizeof(addr);
    if(getsockname(s, (struct sockaddr *)&addr, &alen) == 0)
        bound_port = ntohs(addr.sin_port);
    else
        bound_port = port;

    accept_thread = std::thread(&Server::acceptLoop, this);
}

Server::~Server(){
    stopping = true;
    accept_thread.join();
    std::vector<Client*> cs = getClients();
    for(Client *c : cs)
        closeClient(c);
    close(s);
}

void Server::acceptLoop(){
    while(!stopping){
        struct pollfd pfd;
        pfd.fd = s;
        pfd.events = POLLIN;
        pfd.revents = 0;
        if(poll(&pfd, 1, ACCEPT_POLL_MS) <= 0)
            continue;

        struct sockaddr_in client_addr;
        socklen_t len = sizeof(client_addr);
        int ss = accept(s, (struct sockaddr *)&client_addr, &len);
        if(ss < 0)
            continue;

        // A send that blocks for a long time would stall the tick, so give up quickly.
        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        setsockopt(ss, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(ss, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        int yes = 1;
        setsockopt(ss, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));

        char ip[INET_ADDRSTRLEN] = "?";
        inet_ntop(AF_INET, &client_addr.sin_addr, ip, sizeof(ip));

        Client *c = new Client(this, ss, ip);
        {
            std::lock_guard<std::mutex> lock(clients_mtx);
            clients[ss] = c;
        }
        NetEvent e;
        e.type = NetEvent::CONNECT;
        e.client = c;
        pushEvent(e);
        c->start();
    }
}

void Server::pushEvent(const NetEvent &e){
    std::lock_guard<std::mutex> lock(events_mtx);
    events.push_back(e);
}

size_t Server::pollEvents(std::vector<NetEvent> &out){
    std::lock_guard<std::mutex> lock(events_mtx);
    size_t n = events.size();
    out.insert(out.end(), events.begin(), events.end());
    events.clear();
    return n;
}

void Server::closeClient(Client *c){
    {
        std::lock_guard<std::mutex> lock(clients_mtx);
        clients.erase(c->id());
    }
    delete c;
    // Drop queued events of this client, since the pointer is now dangling
    std::lock_guard<std::mutex> lock(events_mtx);
    for(auto itr = events.begin(); itr != events.end(); ){
        if(itr->client == c)
            itr = events.erase(itr);
        else
            ++itr;
    }
}

void Server::broadcast(const std::string &data){
    for(Client *c : getClients())
        c->send(data);
}

std::vector<Client*> Server::getClients(){
    std::lock_guard<std::mutex> lock(clients_mtx);
    std::vector<Client*> v;
    for(auto &kv : clients)
        v.push_back(kv.second);
    return v;
}

size_t Server::clientCount(){
    std::lock_guard<std::mutex> lock(clients_mtx);
    return clients.size();
}
