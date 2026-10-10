// BombConsole server: lobby -> round(s) -> result -> lobby, at a fixed tick.
// Network events are drained once per tick; nothing else touches the game state.

#include <algorithm>
#include <chrono>
#include <ctype.h>
#include <map>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <thread>
#include <time.h>
#include <unistd.h>
#include <vector>

#include "Config.hpp"
#include "Game.hpp"
#include "Json.hpp"
#include "ServerClient.hpp"
#include "Ui.hpp"

static volatile sig_atomic_t g_stop = 0;
static void onSignal(int){ g_stop = 1; }

struct Member{
    Client *client;
    int id;                 // player number 0..7
    std::string name;
    bool ready = false;
    bool spectator = false; // joined during a round: watches until the next lobby
    bool playing = false;   // in the current round
};

class Session{
    public:
    Session(const Config &cfg_, Server &server_, Ui &ui_);
    void run();

    private:
    // lobby
    void handleLobbyLine(Client *c, const std::string &line);
    void handleHello(Client *c, const std::string &rest);
    void removeClient(Client *c);
    Member *findMember(Client *c);
    std::string uniqueName(const std::string &base);
    std::string lobbyJson(int countdown) const;
    void broadcastLobby(int countdown);
    void drawLobby(int countdown);
    bool allReady() const;
    void playRound();
    void finishRound(Game &game);

    const Config &cfg;
    Server &server;
    Ui &ui;
    std::vector<Member> members;
    int round = 1;
    std::map<int, int> score;       // player id -> session score
    std::map<int, int> kills_total;
    unsigned seed_base;
    std::vector<std::string> last_result;    // shown on the server screen
};

static std::string sanitizeName(std::string s){
    std::string out;
    for(unsigned char c : s)
        if(c >= 0x20 && c != 0x7f)
            out += (char)c;
    // trim
    while(!out.empty() && out.back() == ' ') out.pop_back();
    while(!out.empty() && out[0] == ' ') out.erase(0, 1);
    const size_t MAX = 24;
    if(out.size() > MAX){
        out.resize(MAX);
        while(!out.empty() && ((unsigned char)out.back() & 0xC0) == 0x80)
            out.pop_back();     // don't cut a UTF-8 sequence in the middle
        if(!out.empty() && ((unsigned char)out.back() & 0xC0) == 0xC0)
            out.pop_back();
    }
    if(out.empty())
        out = "player";
    return out;
}

Session::Session(const Config &cfg_, Server &server_, Ui &ui_) : cfg(cfg_), server(server_), ui(ui_){
    seed_base = cfg.seed ? cfg.seed : (unsigned)time(nullptr) ^ (unsigned)getpid();
}

Member *Session::findMember(Client *c){
    for(Member &m : members)
        if(m.client == c)
            return &m;
    return nullptr;
}

std::string Session::uniqueName(const std::string &base){
    std::string name = base;
    for(int n = 2; ; n++){
        bool dup = false;
        for(const Member &m : members)
            if(m.name == name)
                dup = true;
        if(!dup)
            return name;
        name = base + "(" + std::to_string(n) + ")";
    }
}

void Session::handleHello(Client *c, const std::string &rest){
    if(findMember(c) != nullptr)
        return;         // already joined
    // "HELLO <proto> <name...>"
    int proto = 0;
    std::string name;
    {
        size_t sp = rest.find(' ');
        std::string p = sp == std::string::npos ? rest : rest.substr(0, sp);
        proto = atoi(p.c_str());
        if(sp != std::string::npos)
            name = rest.substr(sp + 1);
    }
    if(proto != 1){
        c->send("{\"t\":\"error\",\"msg\":\"unsupported protocol\"}\n");
        server.closeClient(c);
        return;
    }
    Member m;
    m.client = c;
    m.name = uniqueName(sanitizeName(name));
    // lowest free id
    for(m.id = 0; ; m.id++){
        bool used = false;
        for(const Member &o : members)
            if(o.id == m.id)
                used = true;
        if(!used)
            break;
    }
    members.push_back(m);
    c->send("{\"t\":\"welcome\",\"you\":" + json::num(m.id) + ",\"proto\":1,\"name\":" + json::str(m.name)
        + ",\"server\":" + json::str(cfg.name) + "}\n");
    ui.log("joined: " + m.name + " (" + c->address() + ")");
}

void Session::removeClient(Client *c){
    for(size_t i = 0; i < members.size(); i++){
        if(members[i].client == c){
            ui.log("left: " + members[i].name);
            members.erase(members.begin() + (long)i);
            break;
        }
    }
    server.closeClient(c);
}

void Session::handleLobbyLine(Client *c, const std::string &line){
    size_t sp = line.find(' ');
    std::string cmd = line.substr(0, sp);
    std::string rest = sp == std::string::npos ? "" : line.substr(sp + 1);
    if(cmd == "HELLO"){
        handleHello(c, rest);
        return;
    }
    Member *m = findMember(c);
    if(m == nullptr)
        return;         // must say HELLO first
    if(cmd == "READY")
        m->ready = rest != "0";
    else if(cmd == "BYE")
        removeClient(c);
    // IN is ignored in the lobby
}

std::string Session::lobbyJson(int countdown) const{
    std::string s = "{\"t\":\"lobby\",\"players\":[";
    bool first = true;
    for(const Member &m : members){
        if(!first) s += ",";
        first = false;
        s += "{\"id\":" + json::num(m.id) + ",\"name\":" + json::str(m.name) + ",\"ready\":" + json::boolean(m.ready)
            + ",\"score\":" + json::num(score.count(m.id) ? score.at(m.id) : 0) + "}";
    }
    s += "],\"countdown\":" + (countdown >= 0 ? json::num(countdown) : std::string("null"))
        + ",\"min\":" + json::num(cfg.min_players) + ",\"max\":" + json::num(cfg.max_players)
        + ",\"round\":" + json::num(round) + ",\"rounds\":" + json::num(cfg.rounds) + "}";
    return s;
}

void Session::broadcastLobby(int countdown){
    server.broadcast(lobbyJson(countdown) + "\n");
}

bool Session::allReady() const{
    int n = 0;
    for(const Member &m : members){
        if(!m.ready)
            return false;
        n++;
    }
    return n >= cfg.min_players;
}

void Session::drawLobby(int countdown){
    if(!ui.hasScreen())
        return;
    std::vector<std::string> lines;
    char buf[128];
    snprintf(buf, sizeof(buf), "BombConsole server  port %d  round %d/%d", server.port(), round, cfg.rounds);
    lines.push_back(buf);
    lines.push_back("waiting for players...  g: force start   q: quit");
    if(countdown >= 0){
        snprintf(buf, sizeof(buf), "starting in %d s", countdown);
        lines.push_back(buf);
    }
    lines.push_back("");
    for(const Member &m : members){
        snprintf(buf, sizeof(buf), "  %d. %-16s %s  score %d", m.id + 1, m.name.c_str(), m.ready ? "READY" : "     ",
            score.count(m.id) ? score[m.id] : 0);
        lines.push_back(buf);
    }
    if(members.empty())
        lines.push_back("  (nobody yet)");
    lines.push_back("");
    for(const std::string &l : last_result)
        lines.push_back(l);
    ui.drawLobby(lines);
}

void Session::run(){
    typedef std::chrono::steady_clock clock;
    std::vector<NetEvent> events;
    bool counting = false;
    clock::time_point count_end;
    int last_countdown = -2;
    bool force = false;

    while(!g_stop){
        // ---- lobby ----
        for(Member &m : members){
            m.spectator = false;
            m.playing = false;
        }
        counting = false;
        last_countdown = -2;
        force = false;
        broadcastLobby(-1);
        drawLobby(-1);

        while(!g_stop){
            auto next = clock::now() + std::chrono::milliseconds(cfg.tick_ms);
            events.clear();
            server.pollEvents(events);
            bool changed = !events.empty();
            for(NetEvent &e : events){
                if(e.type == NetEvent::LINE)
                    handleLobbyLine(e.client, e.line);
                else if(e.type == NetEvent::DISCONNECT)
                    removeClient(e.client);
                // CONNECT: wait for HELLO
            }
            int k = ui.key();
            if(k == 'q')
                g_stop = 1;
            else if(k == 'g' && (int)members.size() >= std::max(1, std::min(cfg.min_players, 2)))
                force = true;

            bool want = allReady() || (cfg.autostart > 0 && (int)members.size() >= cfg.autostart);
            if(force && (int)members.size() >= 1)
                break;
            if(want && !counting){
                counting = true;
                count_end = clock::now() + std::chrono::seconds(cfg.countdown);
                changed = true;
            }
            else if(!want && counting){
                counting = false;
                changed = true;
                last_countdown = -2;
            }
            int countdown = -1;
            if(counting){
                auto left = std::chrono::duration_cast<std::chrono::milliseconds>(count_end - clock::now()).count();
                if(left <= 0)
                    break;
                countdown = (int)((left + 999) / 1000);
                if(countdown != last_countdown){
                    changed = true;
                    last_countdown = countdown;
                }
            }
            if(changed){
                broadcastLobby(countdown);
                drawLobby(countdown);
            }
            std::this_thread::sleep_until(next);
        }
        if(g_stop)
            break;

        // ---- round ----
        playRound();
    }
    server.broadcast("{\"t\":\"end\",\"msg\":\"server closed\"}\n");
}

void Session::playRound(){
    typedef std::chrono::steady_clock clock;
    std::vector<std::pair<int, std::string>> seats;
    std::sort(members.begin(), members.end(), [](const Member &a, const Member &b){ return a.id < b.id; });
    for(Member &m : members){
        if((int)seats.size() < cfg.max_players){
            m.playing = true;
            seats.push_back(std::make_pair(m.id, m.name));
        }
        else
            m.spectator = true;
    }
    unsigned seed = seed_base + (unsigned)round * 7919u;
    Game game(cfg, seed);
    game.start(seats);
    std::string start = game.startJson(round, cfg.rounds);
    server.broadcast(start + "\n");
    ui.log("round " + std::to_string(round) + " start: " + std::to_string(seats.size()) + " players, "
        + std::to_string(game.width()) + "x" + std::to_string(game.height()) + ", seed " + std::to_string(seed));

    std::vector<NetEvent> events;
    auto next = clock::now();
    while(!g_stop && !game.isOver()){
        next += std::chrono::milliseconds(cfg.tick_ms);
        events.clear();
        server.pollEvents(events);
        for(NetEvent &e : events){
            if(e.type == NetEvent::DISCONNECT){
                Member *m = findMember(e.client);
                if(m != nullptr && m->playing)
                    game.disconnect(m->id);
                removeClient(e.client);
            }
            else if(e.type == NetEvent::LINE){
                size_t sp = e.line.find(' ');
                std::string cmd = e.line.substr(0, sp);
                if(cmd == "HELLO"){
                    // late comer: spectator until the next lobby
                    handleHello(e.client, sp == std::string::npos ? "" : e.line.substr(sp + 1));
                    Member *m = findMember(e.client);
                    if(m != nullptr){
                        m->spectator = true;
                        e.client->send(start + "\n");
                    }
                    continue;
                }
                Member *m = findMember(e.client);
                if(m == nullptr)
                    continue;
                if(cmd == "IN" && m->playing){
                    // "IN <U|D|L|R|-> <0|1>"
                    char dir = '-';
                    int bomb = 0;
                    if(sscanf(e.line.c_str(), "IN %c %d", &dir, &bomb) >= 1)
                        game.input(m->id, dir, bomb != 0);
                }
                else if(cmd == "BYE"){
                    if(m->playing)
                        game.disconnect(m->id);
                    removeClient(e.client);
                }
            }
        }
        int k = ui.key();
        if(k == 'q')
            g_stop = 1;
        game.tick();
        server.broadcast(game.stateJson() + "\n");
        if(ui.hasScreen()){
            std::vector<std::string> lines;
            char buf[160];
            snprintf(buf, sizeof(buf), "round %d/%d  tick %d  left %.1f  q: abort", round, cfg.rounds, game.tickCount(),
                game.secondsLeft());
            lines.push_back(buf);
            for(const GamePlayer &p : game.players()){
                snprintf(buf, sizeof(buf), " %d %-12s %-6s bombs %d/%d fire %d", p.id + 1, p.name.c_str(),
                    p.alive() ? "alive" : p.st == GamePlayer::St::DYING ? "dying" : "dead",
                    p.max_bombs - p.bombs_placed, p.max_bombs, p.power);
                lines.push_back(buf);
            }
            ui.drawGame(game, lines);
        }
        std::this_thread::sleep_until(next);
    }
    finishRound(game);
}

void Session::finishRound(Game &game){
    std::vector<int> rank = game.ranking();
    const std::vector<GamePlayer> &ps = game.players();
    std::vector<int> round_score(ps.size(), 0);
    for(size_t i = 0; i < ps.size(); i++){
        if(cfg.rounds > 1 && rank[i] == 1 && ps.size() > 1)
            score[ps[i].id] += 1;
        kills_total[ps[i].id] += ps[i].kills;
        round_score[i] = score.count(ps[i].id) ? score[ps[i].id] : 0;
    }
    std::string result = game.resultJson(round, cfg.rounds, round_score);
    server.broadcast(result + "\n");

    last_result.clear();
    last_result.push_back("result of round " + std::to_string(round) + ":");
    for(size_t i = 0; i < ps.size(); i++){
        const GamePlayer &p = ps[i];
        char buf[160];
        std::string by;
        if(p.cause == Cause::KILLED){
            for(const GamePlayer &q : ps)
                if(q.id == p.killer)
                    by = " by " + q.name;
        }
        snprintf(buf, sizeof(buf), "  %d. %-16s %s%s  kills %d", rank[i], p.name.c_str(),
            Game::causeName(p.alive() ? Cause::NONE : p.cause), by.c_str(), p.kills);
        last_result.push_back(buf);
    }
    for(const std::string &l : last_result)
        ui.log(l);

    round++;
    if(round > cfg.rounds){
        // session end: total score, then start over
        std::string s = "{\"t\":\"end\",\"rounds\":" + json::num(cfg.rounds) + ",\"score\":[";
        bool first = true;
        for(const Member &m : members){
            if(!first) s += ",";
            first = false;
            s += "{\"id\":" + json::num(m.id) + ",\"name\":" + json::str(m.name)
                + ",\"score\":" + json::num(score.count(m.id) ? score[m.id] : 0)
                + ",\"kills\":" + json::num(kills_total.count(m.id) ? kills_total[m.id] : 0) + "}";
        }
        s += "]}";
        server.broadcast(s + "\n");
        round = 1;
        score.clear();
        kills_total.clear();
    }
    for(Member &m : members)
        m.ready = false;
}

int main(int argc, char **argv){
    Config cfg;
    std::string err;
    bool help = false;
    if(!cfg.parse(argc, argv, err, help)){
        fprintf(stderr, "A_server: %s\n%s", err.c_str(), Config::usage().c_str());
        return 1;
    }
    if(help){
        printf("%s", Config::usage().c_str());
        return 0;
    }
    if(cfg.name.empty()){
        char host[128] = "server";
        if(gethostname(host, sizeof(host)) == 0)
            host[sizeof(host) - 1] = '\0';
        cfg.name = host;
    }
    signal(SIGINT, onSignal);
    signal(SIGTERM, onSignal);
    signal(SIGPIPE, SIG_IGN);

    bool screen = !cfg.no_ui && isatty(STDOUT_FILENO);
#ifdef NO_CURSES
    screen = false;
#endif
    Server server((uint16_t)cfg.port);
    Ui ui(screen);
    ui.log("BombConsole server '" + cfg.name + "' listening on TCP port " + std::to_string(server.port()));
    ui.log("clients: python3 client/A_client.py <this host>");
    {
        Session session(cfg, server, ui);
        session.run();
    }
    return 0;
}
