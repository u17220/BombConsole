#ifndef BOMB_HPP
#define BOMB_HPP

#include "SquareObject.hpp"
#include "Player.hpp"
#include "Fire.hpp"
#include <time.h>

#define BOMB_LIFETIME   2

class Player;   // Forward declaration

class Bomb : public SquareObject{
    public:
    // Constructor
    Bomb(Player *creator_, int x_, int y_, int power_);
    
    // Destructor
    virtual ~Bomb();

    protected:
    virtual req_delete_t run() override;
    virtual void drawAA() override;

    virtual void explosion();

    // Number of squares the fire reaches in direction (dx, dy)
    int fireLength(int dx, int dy);

    Player *creator;

    // explosion time 
    timespec explosion_time;

    // Range of fire (including the center)
    int power;
    
};


#endif