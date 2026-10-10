#ifndef CONFIG_HPP
#define CONFIG_HPP

#include <string>
#include <vector>

// All tunable parameters. Defaults here, overridden by server.conf (key = value),
// then by the command line (--key value). See Config::usage().
struct Config{
    int port = 10001;
    bool no_ui = false;
    std::string name = "";          // server name shown to clients (default: host name)
    std::string conf = "server.conf";
    std::string log = "";           // event log (jsonl), empty = off
    unsigned seed = 0;              // 0 = random

    int tick_ms = 50;
    int rounds = 1;
    int countdown = 5;              // seconds from "all ready" to start
    int min_players = 2;
    int max_players = 8;
    int autostart = 0;              // start as soon as N players joined (0 = ready only)

    // Rules
    double fuse = 2.0;              // seconds
    double fire = 1.0;              // seconds
    int bombs = 4;                  // initial bombs at once
    int bombs_max = 6;
    int power = 3;                  // initial fire length (excluding the center)
    int power_max = 8;
    int speed = 5;                  // ticks per square, initial
    int speed_min = 3;              // fastest
    double dying = 1.0;             // seconds of the death animation
    double time_limit = 0;          // seconds, 0 = none
    double sudden_death = 0;        // seconds before the end when the walls start closing
    int soft_pct = 60;              // % of free squares filled with soft blocks (3+ players)
    int soft_pct_2p = 50;           // same for 2 players (less to dig)
    int item_pct = 35;              // % of soft blocks that drop an item
    int item_fire = 45, item_bomb = 35, item_speed = 20;    // ratio of item kinds
    bool legacy_map = false;        // 4*ceil(sqrt(n))-1 square map (v1 rule) instead of the presets

    // Returns false on error (message in err). help=true when --help was given.
    bool parse(int argc, char **argv, std::string &err, bool &help);
    static std::string usage();

    private:
    bool set(const std::string &key, const std::string &value, std::string &err);
    bool loadFile(const std::string &path, std::string &err, bool must_exist);
};

#endif
