#include <atomic>
#include <vector>
#include <thread>
#include <mutex>
#include <unistd.h>
#include <string.h>
#include <string>
#include <stdio.h>
#include <stdarg.h>
#include <math.h>

#include "ServerClient.hpp"
#include "SquareObject.hpp"
#include "Block.hpp"
#include "Player.hpp"

#include "curses_conf.h"
#ifndef NO_CURSES
#include <ncurses.h>
#include <locale.h>
#else
#include <termios.h>
#include <fcntl.h>

template <typename ... Args>
void printw(const char *format, Args const & ... args){
    printf(format, args ...);
}

int getch(void){
    std::this_thread::sleep_for(std::chrono::microseconds(100000));
    //usleep(100000);
    struct termios oldt, newt;
    int ch;
    int oldf;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    oldf = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, oldf | O_NONBLOCK);
    ch = getchar();
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    fcntl(STDIN_FILENO, F_SETFL, oldf);
    if(ch != EOF){
        return ch;
    }
    return 0;
}

#endif

#define PORT    10001

std::mutex mtx;

std::vector<uint8_t> send_data;   // Map data and Player's death or living

// Messages from the client threads. Printed by the main thread, because curses is not thread safe.
std::vector<std::string> lobby_log;

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

// Close the disconnected clients.
// Their Players are removed before the game starts, and killed during the game.
void close_disconnected(Server &server, bool in_game){
    for(Client *c : server.getClients()){
        if(c->isConnected())
            continue;
        mtx.lock();
        Player *p = Player::find(c->getSocket());
        if(p != nullptr){
            if(in_game){
                p->kill();
            }
            else{
                lobby_log.push_back(std::string("left : ") + p->name);
                p->remove();
            }
        }
        mtx.unlock();
        // Close the socket after removing the Player, so that the socket (= Player's id) is not reused before it
        server.closeClient(c);
    }
}

int main(void){
    #ifndef NO_CURSES
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
    #endif

    // Server initialize
    Server server(PORT);

    ConnectHandler accept_player = [](Client *c){
        mtx.lock();
        new Player(c->getSocket());
        mtx.unlock();
        return true;
    };
    ConnectHandler reject_player = [](Client *){
        return false;
    };
    server.onConnect = accept_player;

    server.onReceive = [](Client *c, uint8_t *receive_data, int len){
        mtx.lock();
        Player *p = Player::find(c->getSocket());
        if(p != nullptr){
            p->appendName(receive_data, len);
            lobby_log.push_back(std::string("name = ") + p->name);
        }
        mtx.unlock();
    };

    // wait clients
    printw("waiting for the players...\n");
    printw("Press 'g' : Game Start ,  Press 'q' : Force End\n\n");
    std::atomic<bool> accept_end(false);
    std::thread waitingPlayers([&accept_end, &server](){
        while(!accept_end)
            server.waitClients();
    });

    std::vector<Client*> clients;
    while(1){
        close_disconnected(server, false);
        mtx.lock();
        for(const std::string &log : lobby_log)
            printw("%s\n", log.c_str());
        lobby_log.clear();
        mtx.unlock();

        int key = getch();
        if(key == 'g' && server.clientCount() > 1){
            // Reject new clients from here.
            // Keep accepting (and rejecting) them until the end, so that they don't wait forever.
            server.onConnect = reject_player;
            close_disconnected(server, false);
            clients = server.getClients();
            if(clients.size() > 1)
                break;
            // Some players left just now
            server.onConnect = accept_player;
        }
        else if(key == 'q'){
            accept_end = true;
            waitingPlayers.join();

            #ifndef NO_CURSES
            endwin();
            #endif
            return 0;
        }

    }

    // make map
    int map_size = 4*ceil(sqrt(clients.size()))-1;
    mtx.lock();
    SquareObject::setMapSize(map_size, map_size);
    make_map();
    send_data = std::vector<uint8_t>(map_size * map_size + 1);
    mtx.unlock();


    // Clients send their first request soon after receiving the initial coordinate,
    // so replace onReceive before sending it.
    server.onReceive = [](Client *c, uint8_t *receive_data, int len){
        if(len < 3)
            return;     // [x, y, bomb]
        mtx.lock();
        Player *p = Player::find(c->getSocket());
        if(p == nullptr){
            mtx.unlock();
            return;
        }
        int x, y;
        int map_size_x, map_size_y;
        SquareObject::getMapSize(&map_size_x, &map_size_y);
        // make data
        std::vector<uint8_t> send_data_(send_data);
        send_data_[0] = p->getState();
        // modify (remove PLAYER_ATTR)
        p->getXY(&x, &y);
        send_data_[1 + map_size_x*y + x] &= ~PLAYER_ATTR;

        x = receive_data[0];
        y = receive_data[1];
        int bomb = receive_data[2];
        p->stepTo(x, y);
        if(bomb)
            p->createBomb();
        mtx.unlock();

        c->Send(send_data_.data(), send_data_.size());
    };


    // send initial coordinate
    uint8_t send_data_coordinate[4];
    uint8_t x_init = 1;
    uint8_t y_init = 1;
    send_data_coordinate[2] = map_size;
    send_data_coordinate[3] = map_size;
    for(Client *c : clients){
        send_data_coordinate[0] = x_init;
        send_data_coordinate[1] = y_init;
        mtx.lock();
        Player *p = Player::find(c->getSocket());
        if(p != nullptr)
            p->moveTo(x_init, y_init);
        mtx.unlock();
        c->Send(send_data_coordinate, sizeof(send_data_coordinate)/sizeof(send_data_coordinate[0]));
        x_init += 4;
        if(x_init >= map_size){
            x_init = 1;
            y_init += 4;
        }
    }


    // game start
    printw("\nGame Start!\n");

    // game loop
    while(Player::living.size() > 1){
        #ifndef NO_CURSES
        clear();
        #endif

        close_disconnected(server, true);

        mtx.lock();
        auto send_data_itr = send_data.begin();
        send_data_itr += 1;
        SquareObject::runAllObjects(send_data_itr);
        mtx.unlock();

        #ifndef NO_CURSES
        refresh();
        #endif
        int key = getch();
        if(key=='q')break;  // Force end
    }

    sleep(1);
    server.onReceive = [](Client *c, uint8_t *, int){
        const uint8_t game_end_command = (uint8_t)'\n';
        c->Send(&game_end_command, 1);
    };
    printw("Game End!\n\n");
    sleep(2);

    mtx.lock();
    while(Player::living.size()){
        auto itr = Player::living.begin();
        Player::death.push_back(*itr);
        Player::living.erase(itr);
    }


    // send ranking
    std::string ranking;
    for(size_t rank = 1; rank <= Player::death.size(); rank++){
        Player *p = Player::death[Player::death.size() - rank];
        ranking += std::to_string(rank);
        ranking += "位 : ";
        ranking += p->name;
        ranking += '\n';
    }
    mtx.unlock();

    #ifndef NO_CURSES
    endwin();
    #endif

    server.broadcast((const uint8_t *)ranking.c_str(), ranking.length());
    printf("RANKING\n%s\n", ranking.c_str());

    accept_end = true;
    waitingPlayers.join();

    return 0;
}
