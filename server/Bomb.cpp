#include "Bomb.hpp"
#include "Fire.hpp"
#include "SquareObject.hpp"
#include "Player.hpp"

#include "curses_conf.h"
#ifndef NO_CURSES
#include <ncurses.h>
#endif

#include <time.h>

Bomb::Bomb(Player *creator_, int x_, int y_, int power_) :
    SquareObject(x_, y_),
    creator(creator_),
    power(power_)
{
    explosion_time.tv_sec = current_time.tv_sec + BOMB_LIFETIME;
    explosion_time.tv_nsec = current_time.tv_nsec;
}


Bomb::~Bomb(){
    if(creator != nullptr)
        creator->removeBomb(this);
}

SquareObject::req_delete_t Bomb::run(){
    if(isElapsed(&explosion_time)){
        explosion();
        return true;
    }
    setSquareMapAttr(x, y, BOMB_ATTR);

    if(getSquareMapAttr(x, y) & FIRE_ATTR){
        //Ignition
        explosion_time.tv_sec -= BOMB_LIFETIME;
    }

    return false;
}

void Bomb::drawAA(){
    #ifndef NO_CURSES
    attron(COLOR_PAIR(BOMB_COLOR));
    mvprintw(y, x*MAP_SQ_SIZE_X, "爆");
    attron(COLOR_PAIR(BOMB_FUSE_COLOR) | A_BLINK);
    mvprintw(y, x*MAP_SQ_SIZE_X+2, "*");
    attroff(COLOR_PAIR(BOMB_FUSE_COLOR) | A_BLINK);
    #endif
}

void Bomb::explosion(){
    new Fire(x, y, x - fireLength(-1, 0), x + fireLength(1, 0), y - fireLength(0, -1), y + fireLength(0, 1));
}

int Bomb::fireLength(int dx, int dy){
    // Block and the edge of the map stop fire before them, Bomb stops fire on it.
    for(int r = 1; r < power; r++){
        int fx = x + dx*r, fy = y + dy*r;
        uint8_t attr = getSquareMapAttr(fx, fy);
        if(!isInMap(fx, fy) || (attr & BLOCK_ATTR))
            return r - 1;
        if(attr & BOMB_ATTR)
            return r;
    }
    return power - 1;
}
