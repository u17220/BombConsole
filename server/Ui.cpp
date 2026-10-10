#include "Ui.hpp"
#include "Game.hpp"

#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#ifndef NO_CURSES
#include <locale.h>
#include <ncurses.h>

enum{ C_WALL = 1, C_SOFT, C_BOMB, C_FIRE, C_ITEM, C_DYING, C_P1, C_P2, C_P3, C_P4, C_P5, C_P6, C_P7, C_P8 };
#endif

Ui::Ui(bool use_curses) : curses(use_curses), stdin_closed(false){
#ifndef NO_CURSES
    if(curses){
        setlocale(LC_ALL, "");
        initscr();
        noecho();
        cbreak();
        keypad(stdscr, TRUE);
        nodelay(stdscr, TRUE);
        curs_set(0);
        if(has_colors()){
            start_color();
            use_default_colors();
            init_pair(C_WALL, COLOR_WHITE, -1);
            init_pair(C_SOFT, COLOR_YELLOW, -1);
            init_pair(C_BOMB, COLOR_BLUE, -1);
            init_pair(C_FIRE, COLOR_RED, -1);
            init_pair(C_ITEM, COLOR_GREEN, -1);
            init_pair(C_DYING, COLOR_MAGENTA, -1);
            int pc[8] = {COLOR_CYAN, COLOR_GREEN, COLOR_YELLOW, COLOR_MAGENTA, COLOR_RED, COLOR_BLUE, COLOR_WHITE, COLOR_CYAN};
            for(int i = 0; i < 8; i++)
                init_pair(C_P1 + i, pc[i], -1);
        }
    }
#else
    curses = false;
#endif
}

Ui::~Ui(){
#ifndef NO_CURSES
    if(curses)
        endwin();
#endif
}

void Ui::log(const std::string &line){
    if(!curses){
        printf("%s\n", line.c_str());
        fflush(stdout);
        return;
    }
    logs.push_back(line);
    if(logs.size() > 200)
        logs.erase(logs.begin(), logs.begin() + 100);
}

int Ui::key(){
#ifndef NO_CURSES
    if(curses){
        int k = getch();
        return k == ERR ? 0 : k;
    }
#endif
    // headless: read a line from stdin when one is available ("g" / "q")
    if(stdin_closed)
        return 0;
    struct pollfd pfd;
    pfd.fd = STDIN_FILENO;
    pfd.events = POLLIN;
    pfd.revents = 0;
    if(poll(&pfd, 1, 0) <= 0)
        return 0;
    char buf[64];
    ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
    if(n <= 0){
        stdin_closed = true;        // EOF or error: stop polling it
        return 0;
    }
    return buf[0];
}

void Ui::drawLog(int from_row){
#ifndef NO_CURSES
    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    (void)cols;
    int avail = rows - from_row;
    if(avail <= 0)
        return;
    size_t start = logs.size() > (size_t)avail ? logs.size() - (size_t)avail : 0;
    for(size_t i = start; i < logs.size(); i++)
        mvprintw(from_row + (int)(i - start), 0, "%s", logs[i].c_str());
#else
    (void)from_row;
#endif
}

void Ui::drawLobby(const std::vector<std::string> &lines){
#ifndef NO_CURSES
    if(!curses)
        return;
    erase();
    int row = 0;
    for(const std::string &l : lines)
        mvprintw(row++, 0, "%s", l.c_str());
    row++;
    drawLog(row);
    refresh();
#else
    (void)lines;
#endif
}

void Ui::drawGame(const Game &game, const std::vector<std::string> &lines){
#ifndef NO_CURSES
    if(!curses)
        return;
    erase();
    std::vector<std::string> rows = game.mapRows();
    int w = game.width(), h = game.height();
    int scr_rows, scr_cols;
    getmaxyx(stdscr, scr_rows, scr_cols);
    if(scr_cols < w * 2 + 1 || scr_rows < h + 3){
        mvprintw(0, 0, "terminal too small: need %dx%d", w * 2 + 1, h + 3);
        refresh();
        return;
    }
    for(int y = 0; y < h; y++){
        for(int x = 0; x < w; x++){
            char c = rows[y][x];
            int pair = 0;
            const char *s = "  ";
            char buf[3] = {c, ' ', 0};
            switch(c){
                case '#': s = "##"; pair = C_WALL; break;
                case '+': s = "++"; pair = C_SOFT; break;
                case 'o': s = "o "; pair = C_BOMB; break;
                case '*': s = "**"; pair = C_FIRE; break;
                case 'x': s = "x "; pair = C_DYING; break;
                case 'F': case 'B': case 'S': s = buf; pair = C_ITEM; break;
                case ' ': s = "  "; break;
                default:
                    s = buf;
                    if(c >= '1' && c <= '8')
                        pair = C_P1 + (c - '1');
            }
            if(pair) attron(COLOR_PAIR(pair));
            mvprintw(y, x * 2, "%s", s);
            if(pair) attroff(COLOR_PAIR(pair));
        }
    }
    int row = h;
    for(const std::string &l : lines)
        mvprintw(row++, 0, "%s", l.c_str());
    drawLog(row + 1);
    refresh();
#else
    (void)game; (void)lines;
#endif
}
