#ifndef CURSES_CONF_H
#define CURSES_CONF_H
#include <cstdint>

//#define NO_CURSES

#ifndef NO_CURSES
#include <ncurses.h>

#define BLOCK_COLOR         1
#define BOMB_COLOR          2
#define BOMB_FUSE_COLOR     3
#define FIRE_COLOR          4
#define PLAYER_LIVING_COLOR 5
#define PLAYER_DYING_COLOR  6

inline void color_init(){
    init_pair(BLOCK_COLOR, COLOR_WHITE, COLOR_BLACK);
    init_pair(BOMB_COLOR, COLOR_BLUE, COLOR_BLACK);
    init_pair(BOMB_FUSE_COLOR, COLOR_YELLOW, COLOR_BLACK);
    init_pair(FIRE_COLOR, COLOR_RED, COLOR_BLACK);
    init_pair(PLAYER_LIVING_COLOR, COLOR_WHITE, COLOR_BLACK);
    init_pair(PLAYER_DYING_COLOR, COLOR_MAGENTA, COLOR_BLUE);
}
#endif

#endif