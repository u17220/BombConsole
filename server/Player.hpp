#ifndef PLAYER_HPP
#define PLAYER_HPP

#include "SquareObject.hpp"
#include "Bomb.hpp"
#include <vector>
#include <unordered_map>
#include <unordered_set>

class Bomb;     // Forward declaration

class Player : public SquareObject{
    public:
    // Constructor
    Player(int id_);
    Player(int ix, int iy);

    // Returns nullptr if not found
    static Player* find(int id_);
    char name[64];
    void appendName(const uint8_t *data, int len);

    void getXY(int *x_, int *y_);

    // Destructor
    ~Player();

    enum class Direction{UP, DOWN, RIGHT, LEFT, NONE};

    void moveTo(Player::Direction direction);
    void moveTo(int x_, int y_);

    // Move to (x_, y_) only if it is next to the current square
    void stepTo(int x_, int y_);

    void createBomb();

    // Called when a Bomb created by this Player is deleted
    void removeBomb(Bomb *bomb);

    // Start dying now (e.g. the client disconnected during the game)
    void kill();

    // Remove from the game (e.g. the client disconnected before the game starts)
    void remove();

    uint8_t getState();

    static std::vector<Player*> death;
    static std::unordered_set<Player*> living;

    private:
    req_delete_t run() override;
    void drawAA() override;

    void init();

    int id;

    // Coordinate in the latest square_map
    int xp, yp;

    // Bomb
    unsigned int creatable_num;
    std::vector<Bomb*> created_bombs;

    enum class StateIs : uint8_t{Living, Dying, Died} state;
    timespec state_transition_time;
    bool removed;

    static std::unordered_map<int, Player*> id_Player;
};


#endif
