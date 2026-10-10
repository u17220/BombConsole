#include "Fire.hpp"
#include "SquareObject.hpp"

#include "curses_conf.h"
#ifndef NO_CURSES
#include <ncurses.h>
#endif 

Fire::Fire(int x_, int y_, int left_fire_x_, int right_fire_x_, int down_fire_y_, int up_fire_y_) :
    SquareObject(x_, y_),
    left_fire_x(left_fire_x_),
    right_fire_x(right_fire_x_),
    up_fire_y(up_fire_y_),
    down_fire_y(down_fire_y_)
{
    fire_time.tv_sec = current_time.tv_sec + FIRE_LIFETIME;
    fire_time.tv_nsec = current_time.tv_nsec;
}

Fire::~Fire(){}

SquareObject::req_delete_t Fire::run(){
    if(isElapsed(&fire_time)){
        return true;
    }
    int r;
    for(r = left_fire_x; r <= right_fire_x; r++)
        setSquareMapAttr(r, y, FIRE_ATTR);
    for(r = down_fire_y; r <= up_fire_y; r++)
        setSquareMapAttr(x, r, FIRE_ATTR);
    return false;
}

void Fire::drawAA(){
    #ifndef NO_CURSES
    int r;
    attron(COLOR_PAIR(FIRE_COLOR));
    for(r = left_fire_x; r <= right_fire_x; r++)
        if((getSquareMapAttr(r, y) & PLAYER_ATTR) == 0x00)
            mvprintw(y, r*MAP_SQ_SIZE_X, "炎");
    for(r = down_fire_y; r <= up_fire_y; r++)
        if((getSquareMapAttr(x, r) & PLAYER_ATTR) == 0x00)
            mvprintw(r, x*MAP_SQ_SIZE_X, "炎");
    attroff(COLOR_PAIR(FIRE_COLOR)); 
    #endif
}