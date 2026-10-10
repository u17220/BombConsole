#include "Config.hpp"

#include <fstream>
#include <sstream>
#include <stdlib.h>

static std::string trim(const std::string &s){
    size_t b = s.find_first_not_of(" \t\r\n");
    if(b == std::string::npos)
        return "";
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

static bool toInt(const std::string &v, int &out){
    char *end = nullptr;
    long x = strtol(v.c_str(), &end, 10);
    if(end == v.c_str() || *end != '\0')
        return false;
    out = (int)x;
    return true;
}

static bool toDouble(const std::string &v, double &out){
    char *end = nullptr;
    double x = strtod(v.c_str(), &end);
    if(end == v.c_str() || *end != '\0')
        return false;
    out = x;
    return true;
}

static bool toBool(const std::string &v, bool &out){
    if(v == "1" || v == "true" || v == "yes" || v == "on" || v == ""){ out = true; return true; }
    if(v == "0" || v == "false" || v == "no" || v == "off"){ out = false; return true; }
    return false;
}

bool Config::set(const std::string &key, const std::string &value, std::string &err){
    bool ok = true;
    if(key == "port") ok = toInt(value, port);
    else if(key == "no-ui" || key == "no_ui") ok = toBool(value, no_ui);
    else if(key == "name") name = value;
    else if(key == "log") log = value;
    else if(key == "seed"){ int s = 0; ok = toInt(value, s); seed = (unsigned)s; }
    else if(key == "tick") ok = toInt(value, tick_ms);
    else if(key == "rounds") ok = toInt(value, rounds);
    else if(key == "countdown") ok = toInt(value, countdown);
    else if(key == "min-players" || key == "min_players") ok = toInt(value, min_players);
    else if(key == "max-players" || key == "max_players") ok = toInt(value, max_players);
    else if(key == "autostart") ok = toInt(value, autostart);
    else if(key == "fuse") ok = toDouble(value, fuse);
    else if(key == "fire") ok = toDouble(value, fire);
    else if(key == "bombs") ok = toInt(value, bombs);
    else if(key == "bombs-max" || key == "bombs_max") ok = toInt(value, bombs_max);
    else if(key == "power") ok = toInt(value, power);
    else if(key == "power-max" || key == "power_max") ok = toInt(value, power_max);
    else if(key == "speed") ok = toInt(value, speed);
    else if(key == "speed-min" || key == "speed_min") ok = toInt(value, speed_min);
    else if(key == "dying") ok = toDouble(value, dying);
    else if(key == "time") ok = toDouble(value, time_limit);
    else if(key == "sudden-death" || key == "sudden_death") ok = toDouble(value, sudden_death);
    else if(key == "soft") ok = toInt(value, soft_pct);
    else if(key == "soft-2p" || key == "soft_2p") ok = toInt(value, soft_pct_2p);
    else if(key == "item") ok = toInt(value, item_pct);
    else if(key == "item-fire" || key == "item_fire") ok = toInt(value, item_fire);
    else if(key == "item-bomb" || key == "item_bomb") ok = toInt(value, item_bomb);
    else if(key == "item-speed" || key == "item_speed") ok = toInt(value, item_speed);
    else if(key == "legacy-map" || key == "legacy_map") ok = toBool(value, legacy_map);
    else{
        err = "unknown option: " + key;
        return false;
    }
    if(!ok)
        err = "invalid value for " + key + ": '" + value + "'";
    return ok;
}

bool Config::loadFile(const std::string &path, std::string &err, bool must_exist){
    std::ifstream in(path.c_str());
    if(!in){
        if(must_exist){
            err = "cannot open config file: " + path;
            return false;
        }
        return true;
    }
    std::string line;
    int lineno = 0;
    while(std::getline(in, line)){
        lineno++;
        size_t hash = line.find('#');
        if(hash != std::string::npos)
            line = line.substr(0, hash);
        line = trim(line);
        if(line.empty())
            continue;
        size_t eq = line.find('=');
        std::string key = trim(eq == std::string::npos ? line : line.substr(0, eq));
        std::string value = eq == std::string::npos ? "" : trim(line.substr(eq + 1));
        if(!set(key, value, err)){
            err = path + ":" + std::to_string(lineno) + ": " + err;
            return false;
        }
    }
    return true;
}

static bool takesValue(const std::string &key){
    return key != "no-ui" && key != "no_ui" && key != "legacy-map" && key != "legacy_map" && key != "help";
}

bool Config::parse(int argc, char **argv, std::string &err, bool &help){
    help = false;
    // 1. find --conf first, so that the command line overrides the file
    std::vector<std::pair<std::string, std::string>> args;
    for(int i = 1; i < argc; i++){
        std::string a = argv[i];
        if(a.compare(0, 2, "--") != 0){
            err = "unexpected argument: " + a;
            return false;
        }
        a = a.substr(2);
        std::string key = a, value;
        size_t eq = a.find('=');
        bool has_value = false;
        if(eq != std::string::npos){
            key = a.substr(0, eq);
            value = a.substr(eq + 1);
            has_value = true;
        }
        if(key == "help" || key == "h"){
            help = true;
            return true;
        }
        if(!has_value && takesValue(key)){
            if(i + 1 >= argc){
                err = "missing value for --" + key;
                return false;
            }
            value = argv[++i];
        }
        else if(!has_value && (key == "legacy-map" || key == "legacy_map") && i + 1 < argc
                && std::string(argv[i + 1]).compare(0, 2, "--") != 0){
            value = argv[++i];
        }
        args.push_back(std::make_pair(key, value));
    }
    bool conf_given = false;
    for(auto &kv : args){
        if(kv.first == "conf"){
            conf = kv.second;
            conf_given = true;
        }
    }
    if(!loadFile(conf, err, conf_given))
        return false;
    for(auto &kv : args){
        if(kv.first == "conf")
            continue;
        if(!set(kv.first, kv.second, err))
            return false;
    }
    if(tick_ms < 10 || tick_ms > 1000){ err = "tick must be 10..1000 ms"; return false; }
    if(min_players < 1) min_players = 1;
    if(max_players < min_players) max_players = min_players;
    if(max_players > 8) max_players = 8;
    if(rounds < 1) rounds = 1;
    return true;
}

std::string Config::usage(){
    return
    "usage: A_server [--key value ...]\n"
    "  --port N          TCP port (10001). 0 = any free port\n"
    "  --no-ui           no ncurses screen; log to stdout\n"
    "  --name S          server name shown to clients (host name)\n"
    "  --conf FILE       config file, key = value per line (server.conf)\n"
    "  --log FILE        write game events as JSON lines\n"
    "  --seed N          random seed (0 = random)\n"
    "  --rounds N        rounds per session (1)\n"
    "  --countdown N     seconds from all-ready to start (5)\n"
    "  --min-players N   players needed to start (2)\n"
    "  --max-players N   players per round, the rest spectate (8)\n"
    "  --autostart N     start as soon as N players joined (0 = wait for ready)\n"
    "  --tick MS         tick length in ms (50)\n"
    "  --fuse S          bomb fuse in seconds (2.0)\n"
    "  --fire S          fire duration in seconds (1.0)\n"
    "  --bombs N         initial bombs at once (4), --bombs-max N (6)\n"
    "  --power N         initial fire length (3), --power-max N (8)\n"
    "  --speed N         ticks per square (5), --speed-min N (3)\n"
    "  --dying S         death animation in seconds (1.0)\n"
    "  --time S          round time limit in seconds (0 = none)\n"
    "  --sudden-death S  seconds before the end when walls close in (0 = off)\n"
    "  --soft PCT        soft block density in percent (60), --soft-2p PCT for 2 players (50)\n"
    "  --item PCT        chance that a soft block drops an item (35)\n"
    "  --item-fire N --item-bomb N --item-speed N   item ratio (45:35:20)\n"
    "  --legacy-map      v1 square map 4*ceil(sqrt(n))-1 instead of the presets by player count\n";
}
