#ifndef SQUARE_OBJECT_HPP
#define SQUARE_OBJECT_HPP

#define MAP_SQ_SIZE_X   3
#include <cstdint>
#include <list>
#include <vector>

#include "curses_conf.h"
#ifndef NO_CURSES
#include <ncurses.h>
#include <locale.h>
#endif
#include <time.h>

class SquareObject{
    public:
    // Constructor
    SquareObject();
    SquareObject(int x_, int y_);

    // Destructor
    virtual ~SquareObject();

    static void setMapSize(int map_size_x, int map_size_y);
    static void getMapSize(int *map_size_x, int *map_size_y);
    static bool isInMap(int map_x, int map_y);

    // Execute every time
    static void runAllObjects();
    static void runAllObjects(std::vector<uint8_t>::iterator &square_map_cylinder);

    protected:
    static int MAP_SIZE_X;
    static int MAP_SIZE_Y;

    // Coordinate
    int x, y;

    // If the object required delete itself.
    typedef bool req_delete_t;

    // Executed every time
    virtual req_delete_t run() = 0;
    virtual void drawAA() = 0;

    void print(const char *aa);

    // Add attribute of next square_map
    static uint8_t setSquareMapAttr(int map_x, int map_y, uint8_t map_attr);

    // Get attribute of previous square_map
    static uint8_t getSquareMapAttr(int map_x, int map_y);

    static bool isElapsed(timespec *timepoint);
    static timespec current_time;

    private:
    // Next map
    static std::vector<std::vector<uint8_t>> square_map;

    // Previous map
    static std::vector<std::vector<uint8_t>> square_map_p;

    // Object list
    static std::list<SquareObject*> object_list;
};

// Map attributes
const uint8_t PLAYER_LIVING_ATTR    = 0x80;
const uint8_t PLAYER_DYING_ATTR     = 0x40;
const uint8_t PLAYER_ATTR           = PLAYER_DYING_ATTR + PLAYER_LIVING_ATTR;
const uint8_t BLOCK_ATTR            = 0x20;
const uint8_t BOMB_ATTR             = 0x10;
const uint8_t FIRE_ATTR             = 0x08;
const uint8_t THREAT_ATTR           = 0x04;

#endif
