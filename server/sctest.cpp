#include "ServerClient.hpp"
#include <stdio.h>
#include <unistd.h>

int main(void){
    Server server(8000);
    server.onConnect = [](Client *c){
        printf("connected %d\n", c->getSocket());
        return true;
    };
    server.onReceive = [](Client *c, uint8_t *receive_data, int len){
        printf("from %d len=%d %.*s", c->getSocket(), len, len, (const char *)receive_data);
        c->Send(receive_data, len);
    };
    printf("Server Initialized\n");
    int i = 0;
    while(i < 3){
        if(server.waitClients() != nullptr)
            i++;
    }

    return 0;
}
