#ifndef SERVER_CLIENT_HPP
#define SERVER_CLIENT_HPP

// TCP server with one reader thread per connection.
// Received lines and connection changes are queued as NetEvents, and the main
// thread drains the queue once per tick (Server::pollEvents). Nothing in the
// game logic runs on the network threads.

#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

class Server;

class Client{
    public:
    friend Server;
    ~Client();

    // Send the bytes as they are (the caller appends '\n'). Thread safe.
    // Returns false if the connection is broken.
    bool send(const std::string &data);

    int id() const { return ss; }
    bool isConnected() const { return connected; }
    const std::string &address() const { return addr; }

    private:
    Client(Server *server_, int ss_, const std::string &addr_);
    void start();
    void task();

    Server *server;
    int ss;
    std::string addr;
    std::thread *t;
    std::atomic<bool> connected;
    std::mutex send_mtx;
};

struct NetEvent{
    enum Type{ CONNECT, LINE, DISCONNECT } type;
    Client *client;
    std::string line;
};

class Server{
    public:
    friend Client;
    explicit Server(uint16_t port);
    ~Server();

    // Port actually bound (useful when port 0 was requested)
    uint16_t port() const { return bound_port; }

    // Move all pending events into out. Returns the number of events.
    size_t pollEvents(std::vector<NetEvent> &out);

    void broadcast(const std::string &data);

    // Close the socket and free the Client. Its pointer must not be used afterwards.
    // Safe to call from the main thread after a DISCONNECT event, or at any time to kick.
    void closeClient(Client *c);

    std::vector<Client*> getClients();
    size_t clientCount();

    private:
    void acceptLoop();
    void pushEvent(const NetEvent &e);

    int s;
    uint16_t bound_port;
    std::thread accept_thread;
    std::atomic<bool> stopping;

    std::mutex clients_mtx;
    std::unordered_map<int, Client*> clients;

    std::mutex events_mtx;
    std::deque<NetEvent> events;
};

#endif
