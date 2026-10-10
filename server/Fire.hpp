#ifndef FIRE_HPP
#define FIRE_HPP

#include "SquareObject.hpp"
#include <time.h>

#define FIRE_LIFETIME 1

class Fire : public SquareObject{
    public:
    // Constructor
    Fire(int x_, int y_, int left_fire_x_, int right_fire_x_, int down_fire_y_, int up_fire_y_);

    // Destructor
    virtual ~Fire();

    protected:
    virtual req_delete_t run() override;
    virtual void drawAA() override;

    const int left_fire_x;
    const int right_fire_x;
    const int up_fire_y;
    const int down_fire_y;

    timespec fire_time;
};

#endif