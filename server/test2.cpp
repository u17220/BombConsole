#include <ncurses.h>
#include <locale.h>
#include <vector>
#include <atomic>
#include <thread>
#include <mutex>
#include <unistd.h>
#include <string>
#include <string.h>

#include "ServerClient.hpp"
#include "SquareObject.hpp"
#include "Block.hpp"
#include "Player.hpp"
#include "curses_conf.h"

#define PORT    8000

std::mutex mtx;



Player *gm;
void gamemaster(int key){
    switch (key){
        case KEY_UP:    gm->moveTo(Player::Direction::UP);      break;
        case KEY_DOWN:  gm->moveTo(Player::Direction::DOWN);    break;
        case KEY_LEFT:  gm->moveTo(Player::Direction::LEFT);    break;
        case KEY_RIGHT: gm->moveTo(Player::Direction::RIGHT);   break;
        case ' ':       gm->createBomb();                       break;
        default:    break;
    }
}

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
    
    // test
    SquareObject::setMapSize(7, 7);
    make_map();

    gm = new Player(3, 5);


    Server server(PORT);    
    server.onConnect = [](Client *c){
        Player *p = new Player(c->getSocket());
        p->moveTo(3, 3);
        return true;
    };

    server.onReceive = [](Client *c, uint8_t *receive_data, int len){
        Player *p = Player::find(c->getSocket());
        p->appendName(receive_data, len);
    };

    // wait client
    while(server.waitClients() == nullptr);
    int map_size_x, map_size_y;
    SquareObject::getMapSize(&map_size_x, &map_size_y);
    std::vector<uint8_t> send_data(map_size_y * map_size_x + 1);   // Map data and Player's death or living

    while(1){
        clear();
        mtx.lock();
        auto itr = send_data.begin();
        itr += 1;
        SquareObject::runAllObjects(itr);

        // send data 
        for(Client *c : server.getClients()){
            Player *p = Player::find(c->getSocket());
            send_data[0] = p->getState();
            c->Send(send_data.data(), (int)send_data.size());
        }

        mtx.unlock();
        refresh();
        int key = getch();
        if(key=='q')break;
        gamemaster(key);
    }

    // game end
    uint8_t game_end_command = (uint8_t)'\n';
    server.broadcast(&game_end_command, 1);

    endwin();

    return 0;
}