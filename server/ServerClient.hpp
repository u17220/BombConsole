#ifndef SERVER_CLIENT_HPP
#define SERVER_CLIENT_HPP

#define CLIENT_BUFFER_SIZE  32
#define SEND_RCV_TIMEOUT    1       //[s]
#define ACCEPT_TIMEOUT      100     //[ms]

#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

class Server;
class Client;

typedef bool (*ConnectHandler)(Client *c);
typedef void (*ReceiveHandler)(Client *c, uint8_t *receive_data, int len);

class Client{
    public:
    friend Server;

    // Destructor
    ~Client();

    int Send(const uint8_t *send_data, int len);

    int getSocket();

    // false after the peer closed the connection (or a socket error occurred)
    bool isConnected();

    private:
    // Constructor
    Client(int ss_, std::atomic<ReceiveHandler> *onReceive_);

    // Start receiving (called by Server after onConnect accepted the client)
    void start();

    // socket
    int ss;
    std::atomic<ReceiveHandler> *onReceive;

    // thread
    std::thread *t;
    void task();
    std::atomic<bool> isEnd;
    std::atomic<bool> connected;

    // Send() can be called from several threads
    std::mutex send_mtx;

    // Buffer
    uint8_t buf[CLIENT_BUFFER_SIZE];
};

//

class Server{
    public:
    // Constructor
    Server(uint16_t port);

    // Destructor
    ~Server();

    // Handlers can be replaced while clients are running
    std::atomic<ConnectHandler> onConnect;
    std::atomic<ReceiveHandler> onReceive;

    // Wait for a client up to ACCEPT_TIMEOUT.
    // Returns nullptr on timeout, error or rejection by onConnect.
    Client* waitClients();

    void broadcast(const uint8_t *send_data, int len);

    void closeClient(Client *c);

    // Snapshot of the accepted clients
    std::vector<Client*> getClients();
    size_t clientCount();

    private:
    int s;

    // Guards clients (written by the accepting thread, read by the others)
    std::mutex clients_mtx;
    std::unordered_map<int, Client*> clients;
};


#endif
