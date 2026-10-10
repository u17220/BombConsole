#ifndef GAME_HPP
#define GAME_HPP

// One round of the game: map, players, bombs and fire, advanced by tick().
// Everything is deterministic given the Config, the seed and the inputs.

#include <random>
#include <string>
#include <vector>

#include "Config.hpp"

enum class Cause{ NONE, KILLED, SUICIDE, SUDDEN_DEATH, DISCONNECT, TIMEOUT };

struct GamePlayer{
    int id = 0;                 // player number 0..7 (stable within a session)
    std::string name;
    int x = 0, y = 0;
    int start_x = 0, start_y = 0;
    enum class St{ ALIVE, DYING, DEAD } st = St::ALIVE;
    int dying_until = 0;        // tick when DYING becomes DEAD
    int death_tick = -1;        // tick of death (-1 = alive)
    int max_bombs = 1;
    int bombs_placed = 0;
    int power = 2;
    int speed_ticks = 5;        // ticks per square
    int move_cd = 0;
    char dir = '-';             // pending input
    bool bomb_req = false;
    int kills = 0;
    Cause cause = Cause::NONE;
    int killer = -1;
    bool connected = true;
    int items = 0;              // items picked up
    bool alive() const { return st == St::ALIVE; }
};

struct Cell{
    char kind = ' ';            // ' ' free, '#' hard, '+' soft
    char item = 0;              // 0, 'F' fire, 'B' bomb, 'S' speed
    int fire_until = 0;         // tick until which fire burns (0 = none)
    int fire_owner = -1;
};

struct Bomb{
    int x, y;
    int owner;
    int power;
    int fuse;                   // ticks left
};

struct GameEvent{               // for the result message and the event log
    std::string type;           // "kill", "suicide", "suddendeath", "disconnect", "timeout", "bomb", "explode", "item"
    int tick;
    int actor;                  // player id
    int target;                 // player id or -1
    std::string extra;          // e.g. item kind
};

class Game{
    public:
    Game(const Config &cfg_, unsigned seed);

    // Start a round. players: (id, name) in seat order.
    void start(const std::vector<std::pair<int, std::string>> &players);

    void input(int id, char dir, bool bomb);
    void disconnect(int id);

    void tick();
    bool isOver() const { return over; }
    int tickCount() const { return ticks; }
    double secondsLeft() const;     // < 0 when there is no limit
    bool suddenDeath() const { return sd_start_tick > 0 && ticks >= sd_start_tick; }

    int width() const { return w; }
    int height() const { return h; }
    const std::vector<GamePlayer> &players() const { return ps; }
    const std::vector<Bomb> &bombs() const { return bs; }
    const Cell &cell(int x, int y) const { return grid[y][x]; }

    // Map as one string per row (one char per square), as sent to clients.
    std::vector<std::string> mapRows() const;
    std::vector<std::string> threatRows() const;

    std::string startJson(int round, int rounds) const;
    std::string stateJson() const;
    // ranking + events. score: total session score per player id (same order as ps)
    std::string resultJson(int round, int rounds, const std::vector<int> &score) const;

    // Rank of each player (1 = winner), computed from death ticks. Same tick = same rank.
    std::vector<int> ranking() const;
    const std::vector<GameEvent> &events() const { return evs; }
    std::vector<GameEvent> takeNewEvents();     // events since the last call (for the log)

    static const char *causeName(Cause c);

    private:
    void buildMap(int n);
    void placePlayer(GamePlayer &p, int seat, int n);
    void placeSoftBlocks();
    void buildSuddenDeathOrder();
    GamePlayer *find(int id);
    bool blocked(int x, int y) const;   // cannot walk into
    bool bombAt(int x, int y) const;
    void explode(size_t bomb_index, std::vector<size_t> &chain);
    void burn(int x, int y, int owner);
    void kill(GamePlayer &p, Cause cause, int killer);
    void advanceSuddenDeath();
    void dropItem(int x, int y);

    const Config &cfg;
    std::mt19937 rng;
    int w = 0, h = 0;
    std::vector<std::vector<Cell>> grid;
    std::vector<GamePlayer> ps;
    std::vector<Bomb> bs;
    std::vector<GameEvent> evs;
    size_t evs_taken = 0;
    int ticks = 0;
    int fuse_ticks = 40, fire_ticks = 20, dying_ticks = 20;
    int limit_ticks = 0;        // 0 = none
    int over_tick = -1;         // tick when the round ends (set once <= 1 player is alive)
    bool over = false;
    bool timeout = false;
    // sudden death: squares in closing order (outer ring first, clockwise), next index
    std::vector<std::pair<int, int>> sd_order;
    size_t sd_next = 0;
    int sd_start_tick = 0;      // tick when the walls start closing (0 = never)
    int sd_interval = 5;        // ticks between two closings
};

#endif
