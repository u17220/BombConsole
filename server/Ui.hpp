#ifndef UI_HPP
#define UI_HPP

// Server screen. With ncurses it draws the lobby and the map; without it (NO_CURSES
// build or --no-ui) it prints log lines to stdout. In both cases key() is non-blocking
// and never drives the tick.

#include <string>
#include <vector>

class Game;

class Ui{
    public:
    explicit Ui(bool use_curses);
    ~Ui();

    bool hasScreen() const { return curses; }

    // Append a line to the log (lobby messages, results)
    void log(const std::string &line);

    // Returns the key pressed (0 = none). Also reads a line from stdin in headless mode
    // and returns its first character, so that "g" and "q" work there too.
    int key();

    void drawLobby(const std::vector<std::string> &lines);
    void drawGame(const Game &game, const std::vector<std::string> &lines);

    private:
    bool curses;
    bool stdin_closed;
    std::vector<std::string> logs;
    void drawLog(int from_row);
};

#endif
