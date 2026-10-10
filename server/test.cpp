#include <ncurses.h>
#include <locale.h>
#include <vector>
#include <atomic>
#include <thread>
#include <mutex>
#include <unistd.h>

#include "SquareObject.hpp"
#include "Block.hpp"
#include "Player.hpp"
#include "curses_conf.h"


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

    SquareObject::setMapSize(15, 15);

    // test
    make_map();

    gm = new Player(3, 3);

    while(1){
        clear();
        SquareObject::runAllObjects();
        refresh();
        int key = getch();
        if(key=='q')break;
        gamemaster(key);
    }

    endwin();

    return 0;
}