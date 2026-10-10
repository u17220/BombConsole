#include <ncurses.h>
#include <locale.h>
#include <vector>
#include <atomic>
#include <thread>
#include <mutex>
#include <unistd.h>
#include <string>
#include <string.h>
#include <math.h>

#include "ServerClient.hpp"
#include "SquareObject.hpp"
#include "Block.hpp"
#include "Player.hpp"
#include "curses_conf.h"

#define PORT    8000

std::mutex mtx;


void make_map(){
    int i,j;
    int map_size_x, map_size_y;
    SquareObject::getMapSize(&map_size_x, &map_size_y);
    for(i = 0; i < map_size_x; i++)
        for(j = 0; j < map_size_y; j++)
            if(
                (i == 0 || j == 0 || i == map_size_x-1 || j == map_size_y-1)
                ||
                (i%2 + j%2 == 0)
            )
               new Block(i, j);
}

int main(void){   
    // ncurses initialize
    setlocale(LC_ALL, "");
    initscr();
    noecho();
    cbreak();
    keypad(stdscr, TRUE);
    halfdelay(1);
    curs_set(0);
    start_color();
    color_init();


    Server server(PORT);    
    server.onConnect = [](Client *c){
        new Player(c->getSocket());
        return true;
    };

    server.onReceive = [](Client *c, uint8_t *receive_data, int len){
        Player *p = Player::find(c->getSocket());
        p->appendName(receive_data, len);
        printw("%s\n", p->name);
    };

    // waiting Players
    std::atomic<bool> waitend(false);
    std::thread waitingPlayers([&waitend, &server](){
        while(!waitend)
            server.waitClients();
    });
    while(1){
        int key = getch();
        if(key == 'g' && server.clientCount() != 0)
            break;
        else if(key == 'q'){
            waitend = true;
            waitingPlayers.join();
            endwin();
            return 0;
        }
    }
    waitend = true;

    // make map
    SquareObject::setMapSize(7, 7);
    int map_size_x, map_size_y;
    SquareObject::getMapSize(&map_size_x, &map_size_y);
    make_map();

    // send coordinate
    uint8_t send_data[4];
    uint8_t x_init = 1;
    uint8_t y_init = 1;
    for(Client *c : server.getClients()){
        send_data[0] = x_init;
        send_data[1] = y_init;
        send_data[2] = map_size_x;
        send_data[3] = map_size_y;

        x_init += 4;
        if(x_init >= map_size_x){
            x_init = 1;
            y_init += 4;
        }
        c->Send(send_data, sizeof(send_data)/sizeof(send_data[0]));
        Player::death.push_back(Player::find(c->getSocket()));    //for test
    }

    // send ranking
    std::string ranking;
    for(size_t rank = 0; rank < Player::death.size(); rank++){
        Player *p = Player::death[rank];
        ranking += std::to_string(rank+1);
        ranking += "位 : ";
        ranking += p->name;
        ranking += '\n';
    }
    server.broadcast((const uint8_t *)ranking.c_str(), ranking.length());

    endwin();
    waitingPlayers.join();
    return 0;
}