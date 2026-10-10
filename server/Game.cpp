#include "Game.hpp"
#include "Json.hpp"

#include <algorithm>
#include <climits>
#include <math.h>

Game::Game(const Config &cfg_, unsigned seed) : cfg(cfg_), rng(seed){
    fuse_ticks = std::max(1, (int)lround(cfg.fuse * 1000.0 / cfg.tick_ms));
    fire_ticks = std::max(1, (int)lround(cfg.fire * 1000.0 / cfg.tick_ms));
    dying_ticks = std::max(1, (int)lround(cfg.dying * 1000.0 / cfg.tick_ms));
    limit_ticks = cfg.time_limit > 0 ? (int)lround(cfg.time_limit * 1000.0 / cfg.tick_ms) : 0;
    if(limit_ticks > 0 && cfg.sudden_death > 0 && cfg.sd_rate > 0){
        sd_start_tick = std::max(1, limit_ticks - (int)lround(cfg.sudden_death * 1000.0 / cfg.tick_ms));
        sd_interval = std::max(1, (int)lround(1000.0 / cfg.tick_ms / cfg.sd_rate));
    }
}

const char *Game::causeName(Cause c){
    switch(c){
        case Cause::NONE: return "alive";
        case Cause::KILLED: return "killed";
        case Cause::SUICIDE: return "suicide";
        case Cause::SUDDEN_DEATH: return "suddendeath";
        case Cause::DISCONNECT: return "disconnect";
        case Cause::TIMEOUT: return "timeout";
    }
    return "?";
}

// ---- setup ----

void Game::buildMap(int n){
    if(cfg.legacy_map){
        // v1 rule: square map of 4*ceil(sqrt(n))-1
        w = h = 4 * (int)ceil(sqrt((double)std::max(n, 2))) - 1;
    }
    else{
        // presets by player count (improvement-plan 3.1): fit in 80x24 with 3 columns per square
        if(n <= 2){ w = 13; h = 11; }
        else if(n <= 4){ w = 15; h = 13; }
        else if(n <= 6){ w = 19; h = 15; }
        else{ w = 21; h = 17; }
    }
    grid.assign(h, std::vector<Cell>(w));
    for(int y = 0; y < h; y++)
        for(int x = 0; x < w; x++)
            if(x == 0 || y == 0 || x == w - 1 || y == h - 1 || (x % 2 == 0 && y % 2 == 0))
                grid[y][x].kind = '#';
}

void Game::placePlayer(GamePlayer &p, int seat, int n){
    int x = 1, y = 1;
    if(cfg.legacy_map){
        // v1 rule: from (1,1), x += 4, wrap to the next row of y += 4
        for(int i = 0; i < seat; i++){
            x += 4;
            if(x >= w){
                x = 1;
                y += 4;
            }
        }
        if(y >= h - 1)
            y = h - 2;
    }
    else{
        // corners first (opposite corners for 2 players), then the middle of each edge
        int mx = w / 2, my = h / 2;
        if(mx % 2 == 0) mx--;       // not on a pillar
        if(my % 2 == 0) my--;
        const int corner[4][2] = {{1, 1}, {w - 2, h - 2}, {w - 2, 1}, {1, h - 2}};
        const int edge[4][2] = {{mx, 1}, {mx, h - 2}, {1, my}, {w - 2, my}};
        if(seat < 4){ x = corner[seat][0]; y = corner[seat][1]; }
        else{ x = edge[(seat - 4) % 4][0]; y = edge[(seat - 4) % 4][1]; }
        (void)n;
    }
    p.x = p.start_x = x;
    p.y = p.start_y = y;
}

void Game::placeSoftBlocks(){
    int pct = cfg.soft_pct;
    if(ps.size() <= 2)
        pct = cfg.soft_pct_2p >= 0 ? cfg.soft_pct_2p : std::max(0, cfg.soft_pct - 10);
    if(pct <= 0)
        return;
    // protect the start squares and their neighbours so that everybody can move and hide
    std::vector<std::vector<bool>> protect(h, std::vector<bool>(w, false));
    for(const GamePlayer &p : ps){
        const int dx[5] = {0, 1, -1, 0, 0}, dy[5] = {0, 0, 0, 1, -1};
        for(int d = 0; d < 5; d++){
            int x = p.x + dx[d], y = p.y + dy[d];
            if(x >= 0 && y >= 0 && x < w && y < h)
                protect[y][x] = true;
        }
    }
    std::vector<std::pair<int, int>> free;
    for(int y = 0; y < h; y++)
        for(int x = 0; x < w; x++)
            if(grid[y][x].kind == ' ' && !protect[y][x])
                free.push_back(std::make_pair(x, y));
    std::shuffle(free.begin(), free.end(), rng);
    size_t count = free.size() * (size_t)pct / 100;
    for(size_t i = 0; i < count; i++)
        grid[free[i].second][free[i].first].kind = '+';
}

void Game::start(const std::vector<std::pair<int, std::string>> &players){
    int n = (int)players.size();
    buildMap(n);
    ps.clear();
    bs.clear();
    evs.clear();
    evs_taken = 0;
    ticks = 0;
    over = false;
    over_tick = -1;
    timeout = false;
    int seat = 0;
    for(auto &pr : players){
        GamePlayer p;
        p.id = pr.first;
        p.name = pr.second;
        p.max_bombs = cfg.bombs;
        p.power = cfg.power;
        p.speed_ticks = cfg.speed;
        placePlayer(p, seat++, n);
        ps.push_back(p);
    }
    placeSoftBlocks();
    buildSuddenDeathOrder();
}

void Game::buildSuddenDeathOrder(){
    // ring by ring from the outside, clockwise from the top-left of each ring
    sd_order.clear();
    sd_next = 0;
    for(int k = 1; k < std::min(w, h) / 2 + 1; k++){
        int x0 = k, y0 = k, x1 = w - 1 - k, y1 = h - 1 - k;
        if(x0 > x1 || y0 > y1)
            break;
        std::vector<std::pair<int, int>> ring;
        for(int x = x0; x <= x1; x++) ring.push_back(std::make_pair(x, y0));
        for(int y = y0 + 1; y <= y1; y++) ring.push_back(std::make_pair(x1, y));
        if(y1 > y0)
            for(int x = x1 - 1; x >= x0; x--) ring.push_back(std::make_pair(x, y1));
        if(x1 > x0)
            for(int y = y1 - 1; y > y0; y--) ring.push_back(std::make_pair(x0, y));
        if(k == 1 && !ring.empty()){
            // start the outer ring at a random point so that no seat is always crushed first
            std::uniform_int_distribution<size_t> off(0, ring.size() - 1);
            std::rotate(ring.begin(), ring.begin() + (long)off(rng), ring.end());
        }
        for(auto &c : ring)
            if(grid[c.second][c.first].kind != '#')
                sd_order.push_back(c);
    }
}

GamePlayer *Game::find(int id){
    for(auto &p : ps)
        if(p.id == id)
            return &p;
    return nullptr;
}

// ---- inputs ----

void Game::input(int id, char dir, bool bomb){
    GamePlayer *p = find(id);
    if(p == nullptr || !p->alive())
        return;
    if(dir == 'U' || dir == 'D' || dir == 'L' || dir == 'R')
        p->dir = dir;
    if(bomb)
        p->bomb_req = true;
}

void Game::disconnect(int id){
    GamePlayer *p = find(id);
    if(p == nullptr)
        return;
    p->connected = false;
    if(p->alive())
        kill(*p, Cause::DISCONNECT, -1);
}

// ---- rules ----

bool Game::bombAt(int x, int y) const{
    for(const Bomb &b : bs)
        if(b.x == x && b.y == y)
            return true;
    return false;
}

bool Game::blocked(int x, int y) const{
    if(x < 0 || y < 0 || x >= w || y >= h)
        return true;
    if(grid[y][x].kind != ' ')
        return true;
    return bombAt(x, y);
}

void Game::kill(GamePlayer &p, Cause cause, int killer){
    if(!p.alive())
        return;
    p.st = GamePlayer::St::DYING;
    p.dying_until = ticks + dying_ticks;
    p.death_tick = ticks;
    p.cause = cause;
    p.killer = killer;
    GameEvent e;
    e.tick = ticks;
    e.actor = killer;
    e.target = p.id;
    switch(cause){
        case Cause::KILLED:
            e.type = "kill";
            if(GamePlayer *k = find(killer))
                k->kills++;
            break;
        case Cause::SUICIDE: e.type = "suicide"; break;
        case Cause::SUDDEN_DEATH: e.type = "suddendeath"; break;
        case Cause::DISCONNECT: e.type = "disconnect"; break;
        case Cause::TIMEOUT: e.type = "timeout"; break;
        case Cause::NONE: break;
    }
    evs.push_back(e);
}

void Game::burn(int x, int y, int owner){
    Cell &c = grid[y][x];
    if(c.fire_until <= ticks){
        c.fire_owner = owner;       // the first fire on a square owns the kill
    }
    c.fire_until = ticks + fire_ticks;
}

void Game::explode(size_t bi, std::vector<size_t> &chain){
    Bomb b = bs[bi];
    GameEvent e;
    e.type = "explode";
    e.tick = ticks;
    e.actor = b.owner;
    e.target = -1;
    evs.push_back(e);

    burn(b.x, b.y, b.owner);
    const int dx[4] = {1, -1, 0, 0};
    const int dy[4] = {0, 0, 1, -1};
    for(int d = 0; d < 4; d++){
        for(int r = 1; r <= b.power; r++){
            int fx = b.x + dx[d] * r, fy = b.y + dy[d] * r;
            if(fx < 0 || fy < 0 || fx >= w || fy >= h)
                break;
            Cell &c = grid[fy][fx];
            if(c.kind == '#')
                break;                      // hard block: fire stops before it
            burn(fx, fy, b.owner);
            if(c.kind == '+'){
                c.kind = ' ';               // soft block: burns and stops the fire
                dropItem(fx, fy);
                break;
            }
            if(c.item){
                c.item = 0;                 // items burn and stop the fire
                break;
            }
            bool chained = false;
            for(size_t j = 0; j < bs.size(); j++){
                if(bs[j].x == fx && bs[j].y == fy && bs[j].fuse > 0){
                    bs[j].fuse = 0;         // chain reaction in the same tick
                    chain.push_back(j);
                    chained = true;
                }
            }
            if(chained)
                break;
        }
    }
}

void Game::dropItem(int x, int y){
    if(cfg.item_pct <= 0)
        return;
    std::uniform_int_distribution<int> pct(0, 99);
    if(pct(rng) >= cfg.item_pct)
        return;
    int total = cfg.item_fire + cfg.item_bomb + cfg.item_speed;
    if(total <= 0)
        return;
    std::uniform_int_distribution<int> kind(0, total - 1);
    int r = kind(rng);
    grid[y][x].item = r < cfg.item_fire ? 'F' : r < cfg.item_fire + cfg.item_bomb ? 'B' : 'S';
}

void Game::advanceSuddenDeath(){
    if(sd_start_tick <= 0 || ticks < sd_start_tick || sd_next >= sd_order.size())
        return;
    if((ticks - sd_start_tick) % sd_interval != 0)
        return;
    int x = sd_order[sd_next].first, y = sd_order[sd_next].second;
    sd_next++;
    Cell &c = grid[y][x];
    c.kind = '#';
    c.item = 0;
    c.fire_until = 0;
    // a bomb under the wall is removed (its owner gets it back)
    for(size_t i = 0; i < bs.size(); ){
        if(bs[i].x == x && bs[i].y == y){
            if(GamePlayer *o = find(bs[i].owner))
                o->bombs_placed = std::max(0, o->bombs_placed - 1);
            bs.erase(bs.begin() + (long)i);
        }
        else
            i++;
    }
    // players on the square are crushed in step 3 of the next tick; do it now so that
    // the wall and the death appear together
    for(GamePlayer &p : ps)
        if(p.alive() && p.x == x && p.y == y)
            kill(p, Cause::SUDDEN_DEATH, -1);
}

void Game::tick(){
    if(over)
        return;
    ticks++;

    // 1. inputs: bombs first, then one step per player when the cooldown allows
    for(GamePlayer &p : ps){
        if(!p.alive()){
            p.dir = '-';
            p.bomb_req = false;
            continue;
        }
        if(p.bomb_req){
            p.bomb_req = false;
            if(p.bombs_placed < p.max_bombs && !bombAt(p.x, p.y) && grid[p.y][p.x].kind == ' '){
                Bomb b;
                b.x = p.x; b.y = p.y; b.owner = p.id; b.power = p.power; b.fuse = fuse_ticks;
                bs.push_back(b);
                p.bombs_placed++;
                GameEvent e;
                e.type = "bomb"; e.tick = ticks; e.actor = p.id; e.target = -1;
                evs.push_back(e);
            }
        }
        if(p.move_cd > 0)
            p.move_cd--;
        if(p.dir != '-' && p.move_cd == 0){
            int nx = p.x, ny = p.y;
            switch(p.dir){
                case 'U': ny--; break;
                case 'D': ny++; break;
                case 'L': nx--; break;
                case 'R': nx++; break;
            }
            p.dir = '-';            // one tap = one step
            if(!blocked(nx, ny)){
                p.x = nx;
                p.y = ny;
                p.move_cd = p.speed_ticks;
            }
        }
    }

    // 2. bombs: fuse, explosions and chains (same tick)
    std::vector<size_t> chain;
    for(size_t i = 0; i < bs.size(); i++){
        if(bs[i].fuse > 0)
            bs[i].fuse--;
        if(bs[i].fuse == 0)
            chain.push_back(i);
    }
    std::vector<bool> exploded(bs.size(), false);
    for(size_t k = 0; k < chain.size(); k++){
        size_t i = chain[k];
        if(exploded[i])
            continue;
        exploded[i] = true;
        explode(i, chain);
        if(exploded.size() < bs.size())
            exploded.resize(bs.size(), false);
    }
    if(!chain.empty()){
        std::vector<Bomb> rest;
        for(size_t i = 0; i < bs.size(); i++){
            if(exploded[i]){
                if(GamePlayer *o = find(bs[i].owner))
                    o->bombs_placed = std::max(0, o->bombs_placed - 1);
            }
            else
                rest.push_back(bs[i]);
        }
        bs.swap(rest);
    }

    // 3. sudden death closes one square, then items, fire and walls
    if(limit_ticks > 0)
        advanceSuddenDeath();
    for(GamePlayer &p : ps){
        if(!p.alive())
            continue;
        Cell &c = grid[p.y][p.x];
        if(c.item){
            switch(c.item){
                case 'F': p.power = std::min(cfg.power_max, p.power + 1); break;
                case 'B': p.max_bombs = std::min(cfg.bombs_max, p.max_bombs + 1); break;
                case 'S': p.speed_ticks = std::max(cfg.speed_min, p.speed_ticks - 1); break;
            }
            p.items++;
            GameEvent e;
            e.type = "item"; e.tick = ticks; e.actor = p.id; e.target = -1; e.extra = std::string(1, c.item);
            evs.push_back(e);
            c.item = 0;
        }
        if(c.kind == '#')
            kill(p, Cause::SUDDEN_DEATH, -1);
        else if(c.fire_until > ticks)
            kill(p, c.fire_owner == p.id ? Cause::SUICIDE : Cause::KILLED, c.fire_owner);
    }

    // 4. dying -> dead
    for(GamePlayer &p : ps)
        if(p.st == GamePlayer::St::DYING && ticks >= p.dying_until)
            p.st = GamePlayer::St::DEAD;

    // 5. time limit
    if(limit_ticks > 0){
        if(ticks >= limit_ticks && !timeout && over_tick < 0){
            timeout = true;
            over_tick = ticks + dying_ticks;
            GameEvent e;
            e.type = "timeout"; e.tick = ticks; e.actor = -1; e.target = -1;
            evs.push_back(e);
        }
    }

    // 6. end of the round: <= 1 alive, then a short grace period for the animation
    int alive = 0;
    for(const GamePlayer &p : ps)
        if(p.alive())
            alive++;
    if(alive <= 1 && over_tick < 0)
        over_tick = ticks + dying_ticks;
    if(over_tick >= 0 && ticks >= over_tick)
        over = true;
}

double Game::secondsLeft() const{
    if(limit_ticks <= 0)
        return -1;
    return std::max(0, limit_ticks - ticks) * cfg.tick_ms / 1000.0;
}

// ---- output ----

std::vector<std::string> Game::mapRows() const{
    std::vector<std::string> rows(h, std::string(w, ' '));
    for(int y = 0; y < h; y++){
        for(int x = 0; x < w; x++){
            const Cell &c = grid[y][x];
            char ch = c.kind;
            if(c.item)
                ch = c.item;
            rows[y][x] = ch;
        }
    }
    for(const Bomb &b : bs)
        rows[b.y][b.x] = 'o';
    for(const GamePlayer &p : ps){
        if(p.st == GamePlayer::St::ALIVE)
            rows[p.y][p.x] = (char)('1' + p.id);
        else if(p.st == GamePlayer::St::DYING)
            rows[p.y][p.x] = 'x';
    }
    for(int y = 0; y < h; y++)
        for(int x = 0; x < w; x++)
            if(grid[y][x].fire_until > ticks)
                rows[y][x] = '*';
    return rows;
}

std::vector<std::string> Game::threatRows() const{
    // introduced in stage 3
    return std::vector<std::string>();
}

static std::string rowsJson(const std::vector<std::string> &rows){
    std::string s = "[";
    for(size_t i = 0; i < rows.size(); i++){
        if(i) s += ",";
        s += json::str(rows[i]);
    }
    return s + "]";
}

std::string Game::startJson(int round, int rounds) const{
    std::string s = "{\"t\":\"start\",\"round\":" + json::num(round) + ",\"rounds\":" + json::num(rounds)
        + ",\"w\":" + json::num(w) + ",\"h\":" + json::num(h)
        + ",\"tick_ms\":" + json::num(cfg.tick_ms)
        + ",\"time\":" + json::num(cfg.time_limit)
        + ",\"players\":[";
    for(size_t i = 0; i < ps.size(); i++){
        const GamePlayer &p = ps[i];
        if(i) s += ",";
        s += "{\"id\":" + json::num(p.id) + ",\"name\":" + json::str(p.name)
            + ",\"x\":" + json::num(p.x) + ",\"y\":" + json::num(p.y) + "}";
    }
    return s + "]}";
}

std::string Game::stateJson() const{
    std::string s = "{\"t\":\"state\",\"tick\":" + json::num(ticks)
        + ",\"left\":" + json::num(secondsLeft())
        + ",\"sd\":" + json::boolean(suddenDeath())
        + ",\"map\":" + rowsJson(mapRows())
        + ",\"players\":[";
    for(size_t i = 0; i < ps.size(); i++){
        const GamePlayer &p = ps[i];
        if(i) s += ",";
        const char *st = p.st == GamePlayer::St::ALIVE ? "alive" : p.st == GamePlayer::St::DYING ? "dying" : "dead";
        s += "{\"id\":" + json::num(p.id) + ",\"x\":" + json::num(p.x) + ",\"y\":" + json::num(p.y)
            + ",\"st\":\"" + st + "\""
            + ",\"bombs\":" + json::num(p.max_bombs - p.bombs_placed) + ",\"maxb\":" + json::num(p.max_bombs)
            + ",\"fire\":" + json::num(p.power) + ",\"spd\":" + json::num(cfg.speed - p.speed_ticks + 1)
            + ",\"kills\":" + json::num(p.kills) + "}";
    }
    s += "],\"bombs\":[";
    for(size_t i = 0; i < bs.size(); i++){
        const Bomb &b = bs[i];
        if(i) s += ",";
        s += "{\"x\":" + json::num(b.x) + ",\"y\":" + json::num(b.y)
            + ",\"fuse\":" + json::num(b.fuse * cfg.tick_ms / 1000.0) + ",\"owner\":" + json::num(b.owner) + "}";
    }
    s += "]";
    std::vector<std::string> threat = threatRows();
    if(!threat.empty())
        s += ",\"threat\":" + rowsJson(threat);
    return s + "}";
}

std::vector<int> Game::ranking() const{
    // later death = better rank; alive = best. Same tick = same rank.
    std::vector<int> order(ps.size());
    for(size_t i = 0; i < ps.size(); i++)
        order[i] = (int)i;
    auto key = [&](int i){ return ps[i].alive() ? INT_MAX : ps[i].death_tick; };
    std::sort(order.begin(), order.end(), [&](int a, int b){ return key(a) > key(b); });
    std::vector<int> rank(ps.size(), 1);
    for(size_t k = 0; k < order.size(); k++){
        if(k > 0 && key(order[k]) == key(order[k - 1]))
            rank[order[k]] = rank[order[k - 1]];
        else
            rank[order[k]] = (int)k + 1;
    }
    return rank;
}

std::string Game::resultJson(int round, int rounds, const std::vector<int> &score) const{
    std::vector<int> rank = ranking();
    std::vector<int> order(ps.size());
    for(size_t i = 0; i < ps.size(); i++)
        order[i] = (int)i;
    std::sort(order.begin(), order.end(), [&](int a, int b){
        if(rank[a] != rank[b]) return rank[a] < rank[b];
        return ps[a].id < ps[b].id;
    });
    std::string s = "{\"t\":\"result\",\"round\":" + json::num(round) + ",\"rounds\":" + json::num(rounds)
        + ",\"ticks\":" + json::num(ticks) + ",\"timeout\":" + json::boolean(timeout) + ",\"ranking\":[";
    for(size_t k = 0; k < order.size(); k++){
        const GamePlayer &p = ps[order[k]];
        if(k) s += ",";
        s += "{\"id\":" + json::num(p.id) + ",\"name\":" + json::str(p.name) + ",\"rank\":" + json::num(rank[order[k]])
            + ",\"kills\":" + json::num(p.kills) + ",\"items\":" + json::num(p.items)
            + ",\"cause\":\"" + causeName(p.alive() ? Cause::NONE : p.cause) + "\""
            + ",\"by\":" + json::num(p.cause == Cause::KILLED ? p.killer : -1)
            + ",\"score\":" + json::num(order[k] < (int)score.size() ? score[order[k]] : 0) + "}";
    }
    s += "],\"events\":[";
    bool first = true;
    for(const GameEvent &e : evs){
        if(e.type == "kill" || e.type == "suicide" || e.type == "suddendeath" || e.type == "disconnect"){
            if(!first) s += ",";
            first = false;
            s += "{\"type\":" + json::str(e.type) + ",\"tick\":" + json::num(e.tick)
                + ",\"killer\":" + json::num(e.actor) + ",\"victim\":" + json::num(e.target) + "}";
        }
    }
    return s + "]}";
}

std::vector<GameEvent> Game::takeNewEvents(){
    std::vector<GameEvent> out(evs.begin() + (long)evs_taken, evs.end());
    evs_taken = evs.size();
    return out;
}
