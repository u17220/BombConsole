#include "Player.hpp"
#include "SquareObject.hpp"
#include <algorithm>
#include <stdlib.h>
#include <string.h>
#include <time.h>

std::unordered_map<int, Player*> Player::id_Player;
std::vector<Player*> Player::death;
std::unordered_set<Player*> Player::living;

Player::Player(int id_) : id(id_){
    init();
    id_Player[id_] = this;
    living.insert(this);
}

Player::Player(int ix, int iy) : SquareObject(ix, iy), id(-1){
    init();
}

void Player::init(){
    name[0] = '\0';
    xp = x;
    yp = y;
    creatable_num = 4;
    state = StateIs::Living;
    removed = false;
}

Player* Player::find(int id_){
    auto itr = id_Player.find(id_);
    if(itr == id_Player.end())
        return nullptr;
    return itr->second;
}

void Player::appendName(const uint8_t *data, int len){
    size_t cur = strlen(name);
    size_t n = std::min((size_t)len, sizeof(name) - 1 - cur);
    memcpy(name + cur, data, n);
    name[cur + n] = '\0';
}

void Player::getXY(int *x_, int *y_){
    *x_ = xp;
    *y_ = yp;
}

SquareObject::req_delete_t Player::run(){
    if(removed)
        return true;
    xp = x;
    yp = y;
    uint8_t map_attr = getSquareMapAttr(x, y);
    switch (state){
    case StateIs::Living:
        if(map_attr & (BLOCK_ATTR | FIRE_ATTR))
            kill();
        else
            setSquareMapAttr(x, y, PLAYER_LIVING_ATTR);
        break;

    case StateIs::Dying:
        setSquareMapAttr(x, y, PLAYER_DYING_ATTR);
        if(isElapsed(&state_transition_time))
            state = StateIs::Died;
        break;
    case StateIs::Died:
        break;
    }
    return false;
}

void Player::drawAA(){
    switch (state){
    case StateIs::Living:
        print("人");
        break;
    case StateIs::Dying:
        print("死");
        break;
    case StateIs::Died:
        break;
    }
}

Player::~Player(){

}

void Player::moveTo(Player::Direction direction){
    if(state != StateIs::Living)
        return;

    int dest_x = x, dest_y = y;
    switch(direction){
        case Direction::UP:     dest_y--;    break;
        case Direction::DOWN:   dest_y++;    break;
        case Direction::LEFT:   dest_x--;    break;
        case Direction::RIGHT:  dest_x++;    break;
        case Direction::NONE:   return;
    }
    // Check whether the Player can move to destination.
    if(isInMap(dest_x, dest_y) && !(getSquareMapAttr(dest_x, dest_y) & (BLOCK_ATTR | BOMB_ATTR))){
        // Player can move to destination, if there are no Block or Bomb.
        x = dest_x;
        y = dest_y;
    }
}

void Player::moveTo(int x_, int y_){
    x = xp = x_;
    y = yp = y_;
}

void Player::stepTo(int x_, int y_){
    if(state != StateIs::Living)
        return;
    if(abs(x_ - x) + abs(y_ - y) != 1 || !isInMap(x_, y_))
        return;
    // The client checks Block and Bomb with the map it received, so only Block is checked here.
    // A Bomb may be placed after the client's check, and rejecting the move would put
    // the client's own coordinate out of sync with the server.
    if(getSquareMapAttr(x_, y_) & BLOCK_ATTR)
        return;
    x = x_;
    y = y_;
}

void Player::createBomb(){
    if(state == StateIs::Living
        && created_bombs.size() < creatable_num
        && !(getSquareMapAttr(x, y) & (BOMB_ATTR | BLOCK_ATTR)))
        created_bombs.push_back(new Bomb(this, x, y, 4));
}

void Player::removeBomb(Bomb *bomb){
    created_bombs.erase(std::remove(created_bombs.begin(), created_bombs.end(), bomb), created_bombs.end());
}

void Player::kill(){
    if(state != StateIs::Living)
        return;
    state = StateIs::Dying;
    state_transition_time.tv_sec = current_time.tv_sec + 1;
    state_transition_time.tv_nsec = current_time.tv_nsec;
    death.push_back(this);
    living.erase(this);
}

void Player::remove(){
    // The id (socket) may be reused by a new client, so unregister it now.
    // The object itself is deleted by runAllObjects().
    if(find(id) == this)
        id_Player.erase(id);
    living.erase(this);
    removed = true;
}

uint8_t Player::getState(){
    return (uint8_t)state;
}
