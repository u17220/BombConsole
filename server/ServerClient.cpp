#include "ServerClient.hpp"

#include <thread>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <poll.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

Client::Client(int ss_, std::atomic<ReceiveHandler> *onReceive_):
    ss(ss_),
    onReceive(onReceive_),
    t(nullptr),
    isEnd(false),
    connected(true)
{
}

Client::~Client(){
    isEnd = true;
    if(t != nullptr){
        t->join();
        delete t;
    }
    close(ss);
}

void Client::start(){
    t = new std::thread(&Client::task, this);
}

int Client::Send(const uint8_t *send_data, int len){
    std::lock_guard<std::mutex> lock(send_mtx);
    int sent = 0;
    while(sent < len){
        ssize_t r = send(ss, (const void*)(send_data + sent), (size_t)(len - sent), MSG_NOSIGNAL);
        if(r < 0){
            if(errno == EINTR)
                continue;
            return -1;
        }
        sent += r;
    }
    return sent;
}

void Client::task(){
    while(!isEnd){
        int len = read(ss, buf, sizeof(buf) / sizeof(buf[0]));
        if(len > 0){
            ReceiveHandler handler = *onReceive;
            if(handler != nullptr)
                handler(this, buf, len);
        }
        else if(len == 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)){
            // Closed by the peer, or socket error
            break;
        }
    }
    connected = false;
}

int Client::getSocket(){
    return ss;
}

bool Client::isConnected(){
    return connected;
}

//

Server::Server(uint16_t port):
    onConnect(nullptr),
    onReceive(nullptr)
{
    s = socket(AF_INET, SOCK_STREAM, 0);
    if(s < 0){
        perror("ERROR on socket");
        exit(1);
    }
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));   // sin_len / sin_zero must be zero on BSD / macOS
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    // To avoid "Address already in use"
    int yes = 1;
    if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof(yes)) < 0)
    {
        perror("ERROR on setsockopt (REUSEADDR)");
        exit(1);
    }

    if(bind(s, (struct sockaddr *)&addr, sizeof(addr))){
        perror("ERROR on bind");
        exit(1);
    }
    if(listen(s, 64)){
        perror("ERROR on listen");
        exit(1);
    }
}

Client* Server::waitClients(){
    struct pollfd pfd;
    pfd.fd = s;
    pfd.events = POLLIN;
    pfd.revents = 0;
    if(poll(&pfd, 1, ACCEPT_TIMEOUT) <= 0)
        return nullptr;

    struct sockaddr_in client_addr;
    socklen_t len = sizeof(struct sockaddr_in);
    int ss = accept(s, (struct sockaddr *)&client_addr, &len);
    if(ss < 0)
        return nullptr;

    // Set time out
    struct timeval tv;
    tv.tv_sec = SEND_RCV_TIMEOUT;
    tv.tv_usec = 0;
    if(setsockopt(ss, SOL_SOCKET, SO_RCVTIMEO, (char*)&tv, sizeof(tv)) ||
       setsockopt(ss, SOL_SOCKET, SO_SNDTIMEO, (char*)&tv, sizeof(tv))){
        perror("ERROR on setsockopt (RCVTIMEO / SNDTIMEO)");
        close(ss);
        return nullptr;
    }

    // Hold the lock until the client is registered, so that getClients() after replacing
    // onConnect never misses a client accepted by the previous onConnect.
    std::lock_guard<std::mutex> lock(clients_mtx);
    Client *c = new Client(ss, &onReceive);
    ConnectHandler handler = onConnect;
    if(handler == nullptr || !handler(c)){
        delete c;
        return nullptr;
    }
    clients[ss] = c;
    // Start receiving after onConnect, so that onReceive never runs before it
    c->start();
    return c;
}

Server::~Server(){
    for(auto itr = clients.begin(); itr != clients.end(); itr++){
        Client *c = itr->second;
        delete c;
    }
    close(s);
}

void Server::closeClient(Client *c){
    {
        std::lock_guard<std::mutex> lock(clients_mtx);
        clients.erase(c->getSocket());
    }
    delete c;
}

void Server::broadcast(const uint8_t *send_data, int len){
    for(Client *c : getClients())
        c->Send(send_data, len);
}

std::vector<Client*> Server::getClients(){
    std::lock_guard<std::mutex> lock(clients_mtx);
    std::vector<Client*> v;
    for(auto itr = clients.begin(); itr != clients.end(); itr++)
        v.push_back(itr->second);
    return v;
}

size_t Server::clientCount(){
    std::lock_guard<std::mutex> lock(clients_mtx);
    return clients.size();
}
