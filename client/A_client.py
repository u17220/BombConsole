#!/usr/bin/env python3
"""BombConsole client (protocol v2). Python 3 standard library only.

usage: python3 A_client.py [host[:port]] [--name NAME] [--ascii]

  host      server address (default: 127.0.0.1, or $BOMBCONSOLE_SERVER=host:port)
  --name    player name (default: $BOMBCONSOLE_NAME, then the login user name)
  --ascii   draw with ASCII characters only (2 columns per square)

keys: arrows / WASD / hjkl = move, Space or z = bomb, r = ready, q = quit
"""
import curses
import getpass
import json
import locale
import os
import queue
import socket
import sys
import threading
import time

PROTO = 1
DEFAULT_PORT = 10001
DOUBLE_Q_SEC = 1.0      # press q twice within this time to quit during a round

# ---------------------------------------------------------------- arguments


def parse_args(argv):
    opts = {"host": None, "port": DEFAULT_PORT, "name": None, "ascii": False}
    env = os.environ.get("BOMBCONSOLE_SERVER")
    if env:
        opts["host"] = env
    i = 1
    while i < len(argv):
        a = argv[i]
        if a in ("-h", "--help"):
            print(__doc__.strip())
            sys.exit(0)
        elif a == "--name" and i + 1 < len(argv):
            opts["name"] = argv[i + 1]
            i += 1
        elif a.startswith("--name="):
            opts["name"] = a.split("=", 1)[1]
        elif a == "--ascii":
            opts["ascii"] = True
        elif a == "--port" and i + 1 < len(argv):
            opts["port"] = int(argv[i + 1])
            i += 1
        elif a.startswith("-"):
            sys.exit("unknown option: " + a)
        else:
            opts["host"] = a
        i += 1
    if opts["host"] and ":" in opts["host"]:
        h, p = opts["host"].rsplit(":", 1)
        opts["host"] = h
        opts["port"] = int(p)
    if not opts["name"]:
        opts["name"] = os.environ.get("BOMBCONSOLE_NAME") or getpass.getuser()
    if os.environ.get("BOMBCONSOLE_ASCII"):
        opts["ascii"] = True
    return opts


# ---------------------------------------------------------------- network


class Connection:
    """TCP connection. A reader thread turns JSON lines into a queue of dicts."""

    def __init__(self, host, port):
        self.sock = socket.create_connection((host, port), timeout=5)
        self.sock.settimeout(None)
        self.sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self.q = queue.Queue()
        self.closed = False
        self.t = threading.Thread(target=self._reader, daemon=True)
        self.t.start()

    def _reader(self):
        try:
            f = self.sock.makefile("r", encoding="utf-8", errors="replace")
            for line in f:
                line = line.strip()
                if not line:
                    continue
                try:
                    self.q.put(json.loads(line))
                except ValueError:
                    pass
        except OSError:
            pass
        self.closed = True
        self.q.put(None)

    def send(self, text):
        try:
            self.sock.sendall((text + "\n").encode("utf-8"))
        except OSError:
            self.closed = True

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass


# ---------------------------------------------------------------- drawing

# character sets: (hard, soft, bomb, fire, dying, item F, item B, item S, players 1..8, empty)
GLYPH_WIDE = {
    "#": "＃", "+": "田", "o": "爆", "*": "炎", "x": "死",
    "F": "火", "B": "弾", "S": "速", " ": "　",
    "1": "１", "2": "２", "3": "３", "4": "４", "5": "５", "6": "６", "7": "７", "8": "８",
}
GLYPH_ASCII = {c: c for c in "#+o*xFBS12345678 "}

C_WALL, C_SOFT, C_BOMB, C_FIRE, C_ITEM, C_DYING, C_THREAT, C_HUD = range(1, 9)
C_PLAYER = 10   # + player id (0..7)


def init_colors():
    if not curses.has_colors():
        return
    curses.start_color()
    try:
        curses.use_default_colors()
        bg = -1
    except curses.error:
        bg = curses.COLOR_BLACK
    curses.init_pair(C_WALL, curses.COLOR_WHITE, bg)
    curses.init_pair(C_SOFT, curses.COLOR_YELLOW, bg)
    curses.init_pair(C_BOMB, curses.COLOR_BLUE, bg)
    curses.init_pair(C_FIRE, curses.COLOR_RED, bg)
    curses.init_pair(C_ITEM, curses.COLOR_GREEN, bg)
    curses.init_pair(C_DYING, curses.COLOR_MAGENTA, bg)
    curses.init_pair(C_THREAT, curses.COLOR_WHITE, curses.COLOR_RED)
    curses.init_pair(C_HUD, curses.COLOR_CYAN, bg)
    pc = [curses.COLOR_CYAN, curses.COLOR_GREEN, curses.COLOR_YELLOW, curses.COLOR_MAGENTA,
          curses.COLOR_RED, curses.COLOR_BLUE, curses.COLOR_WHITE, curses.COLOR_CYAN]
    for i in range(8):
        curses.init_pair(C_PLAYER + i, pc[i], bg)


def attr(pair, extra=0):
    if curses.has_colors():
        return curses.color_pair(pair) | extra
    return extra


def put(scr, y, x, text, a=0):
    """addstr that ignores writes outside the window."""
    try:
        scr.addstr(y, x, text, a)
    except curses.error:
        pass


class Client:
    def __init__(self, scr, conn, opts):
        self.scr = scr
        self.conn = conn
        self.opts = opts
        self.glyph = GLYPH_ASCII if opts["ascii"] else GLYPH_WIDE
        self.cell_w = 2 if opts["ascii"] else 3
        self.me = None              # my id
        self.my_name = opts["name"]
        self.server_name = ""
        self.lobby = None           # last lobby message
        self.start = None           # last start message
        self.state = None           # last state message
        self.result = None          # last result message (shown until a key is pressed)
        self.end = None
        self.names = {}             # id -> name
        self.ready = False
        self.screen = "lobby"       # lobby | game | result
        self.msg = ""               # one-line notice
        self.last_q = 0
        self.dirty = True
        self.quit = False

    # ---- messages from the server ----

    def handle(self, m):
        t = m.get("t")
        if t == "welcome":
            self.me = m["you"]
            self.my_name = m.get("name", self.my_name)
            self.server_name = m.get("server", "")
        elif t == "lobby":
            self.lobby = m
            for p in m["players"]:
                self.names[p["id"]] = p["name"]
                if p["id"] == self.me:
                    self.ready = p["ready"]
            if self.screen == "game":
                self.screen = "lobby"
        elif t == "start":
            self.start = m
            self.state = None
            self.result = None
            self.names = {p["id"]: p["name"] for p in m["players"]}
            self.screen = "game"
            self.msg = ""
        elif t == "state":
            self.state = m
            if self.screen == "lobby":
                self.screen = "game"    # joined during a round: spectate
        elif t == "result":
            self.result = m
            self.screen = "result"
            self.ready = False
        elif t == "end":
            self.end = m
            if "score" not in m:
                self.msg = m.get("msg", "server closed")
        elif t == "error":
            self.msg = m.get("msg", "error")
        self.dirty = True

    # ---- keys ----

    DIRS = {
        curses.KEY_UP: "U", curses.KEY_DOWN: "D", curses.KEY_LEFT: "L", curses.KEY_RIGHT: "R",
        ord("w"): "U", ord("s"): "D", ord("a"): "L", ord("d"): "R",
        ord("k"): "U", ord("j"): "D", ord("h"): "L", ord("l"): "R",
        ord("W"): "U", ord("S"): "D", ord("A"): "L", ord("D"): "R",
    }

    def key(self, c):
        if c == curses.KEY_RESIZE:
            self.dirty = True
            return
        if self.screen == "result":
            if c in (ord("q"), ord("Q")):
                self.quit = True
            elif c != -1:
                self.screen = "lobby"
                self.dirty = True
                if c in (ord("r"), ord("R")):
                    self.toggle_ready()
            return
        if c in (ord("q"), ord("Q")):
            now = time.time()
            if self.screen == "game" and self.alive() and now - self.last_q > DOUBLE_Q_SEC:
                self.last_q = now
                self.msg = "press q again to quit"
                self.dirty = True
            else:
                self.quit = True
            return
        if self.screen == "lobby":
            if c in (ord("r"), ord("R"), ord(" ")):
                self.toggle_ready()
            return
        # game
        if c in self.DIRS:
            self.conn.send("IN %s 0" % self.DIRS[c])
        elif c in (ord(" "), ord("z"), ord("Z")):
            self.conn.send("IN - 1")

    def toggle_ready(self):
        self.ready = not self.ready
        self.conn.send("READY %d" % (1 if self.ready else 0))

    def alive(self):
        if not self.state:
            return False
        for p in self.state["players"]:
            if p["id"] == self.me:
                return p["st"] == "alive"
        return False

    # ---- drawing ----

    def draw(self):
        self.scr.erase()
        if self.screen == "lobby":
            self.draw_lobby()
        elif self.screen == "game":
            self.draw_game()
        else:
            self.draw_result()
        self.scr.refresh()

    def draw_lobby(self):
        scr = self.scr
        put(scr, 0, 0, "BombConsole  server: %s" % self.server_name, attr(C_HUD, curses.A_BOLD))
        put(scr, 1, 0, "r: ready / not ready    q: quit")
        row = 3
        if self.lobby:
            n = len(self.lobby["players"])
            cd = self.lobby.get("countdown")
            if cd is not None:
                put(scr, 2, 0, "starting in %d s..." % cd, attr(C_FIRE, curses.A_BOLD))
            else:
                need = self.lobby.get("min", 2)
                put(scr, 2, 0, "%d player(s). starts when %d or more are all ready" % (n, need))
            for p in self.lobby["players"]:
                me = p["id"] == self.me
                line = " %d. %-16s %s" % (p["id"] + 1, p["name"][:16], "READY" if p["ready"] else "")
                if self.lobby.get("rounds", 1) > 1:
                    line += "  score %d" % p.get("score", 0)
                put(scr, row, 0, line, attr(C_PLAYER + p["id"] % 8, curses.A_REVERSE if me else 0))
                row += 1
        else:
            put(scr, 2, 0, "connecting...")
        if self.msg:
            put(scr, row + 1, 0, self.msg, attr(C_FIRE))

    def need_size(self):
        if not self.start:
            return (0, 0)
        return (self.start["w"] * self.cell_w, self.start["h"] + 3)

    def draw_game(self):
        scr = self.scr
        st = self.state
        if not st or not self.start:
            put(scr, 0, 0, "waiting for the round...")
            return
        rows, cols = scr.getmaxyx()
        need_w, need_h = self.need_size()
        if cols < need_w or rows < need_h:
            put(scr, 0, 0, "terminal too small: need %dx%d, have %dx%d" % (need_w, need_h, cols, rows), attr(C_FIRE))
            put(scr, 1, 0, "(resize the window, or use --ascii)")
            return
        players = {p["id"]: p for p in st["players"]}
        me = players.get(self.me)
        # HUD (top)
        if me:
            hud = "%s  bombs %d/%d  fire %d  spd %d" % (self.my_name[:8], me["bombs"], me["maxb"], me["fire"], me["spd"])
        else:
            hud = "spectating"
        alive = sum(1 for p in st["players"] if p["st"] == "alive")
        hud += "  alive %d/%d" % (alive, len(st["players"]))
        if st.get("left", -1) >= 0:
            hud += "  time %3.0f" % st["left"]
        put(scr, 0, 0, hud[:cols - 1], attr(C_HUD, curses.A_BOLD))
        # map
        g = self.glyph
        for y, line in enumerate(st["map"]):
            for x, ch in enumerate(line):
                a = 0
                if ch == "#":
                    a = attr(C_WALL)
                elif ch == "+":
                    a = attr(C_SOFT)
                elif ch == "o":
                    a = attr(C_BOMB, curses.A_BOLD)
                elif ch == "*":
                    a = attr(C_FIRE, curses.A_BOLD)
                elif ch == "x":
                    a = attr(C_DYING)
                elif ch in "FBS":
                    a = attr(C_ITEM, curses.A_BOLD)
                elif "1" <= ch <= "8":
                    pid = ord(ch) - ord("1")
                    a = attr(C_PLAYER + pid % 8, curses.A_BOLD | (curses.A_REVERSE if pid == self.me else 0))
                put(scr, 1 + y, x * self.cell_w, g.get(ch, ch), a)
        # legend (bottom)
        row = 1 + self.start["h"]
        legend = []
        for p in st["players"]:
            n = self.names.get(p["id"], "?")[:8]
            tag = "" if p["st"] == "alive" else "x"
            legend.append((p["id"], "%d:%s%s" % (p["id"] + 1, n, tag)))
        x = 0
        for pid, text in legend:
            put(scr, row, x, text, attr(C_PLAYER + pid % 8, curses.A_REVERSE if pid == self.me else 0))
            x += len(text) + 2
        if me and me["st"] != "alive":
            put(scr, row + 1, 0, "YOU DIED - watching the rest. q: quit", attr(C_FIRE, curses.A_BOLD))
        elif self.msg:
            put(scr, row + 1, 0, self.msg, attr(C_FIRE))

    def draw_result(self):
        scr = self.scr
        r = self.result
        if not r:
            return
        title = "ROUND %d/%d RESULT" % (r["round"], r["rounds"])
        if r.get("timeout"):
            title += "  (time up)"
        put(scr, 0, 0, title, attr(C_HUD, curses.A_BOLD))
        row = 2
        for p in r["ranking"]:
            cause = p["cause"]
            if cause == "alive":
                why = "survived"
            elif cause == "killed":
                why = "killed by %s" % self.names.get(p.get("by"), "?")
            elif cause == "suicide":
                why = "own bomb"
            elif cause == "suddendeath":
                why = "crushed (sudden death)"
            elif cause == "disconnect":
                why = "disconnected"
            else:
                why = cause
            line = "%d. %-16s %-24s kills %d" % (p["rank"], p["name"][:16], why, p["kills"])
            if r["rounds"] > 1:
                line += "  score %d" % p.get("score", 0)
            put(scr, row, 0, line, attr(C_PLAYER + p["id"] % 8, curses.A_REVERSE if p["id"] == self.me else 0))
            row += 1
        if self.end and "score" in self.end and self.end.get("rounds", 1) > 1:
            row += 1
            put(scr, row, 0, "SESSION TOTAL", attr(C_HUD, curses.A_BOLD))
            row += 1
            for p in sorted(self.end["score"], key=lambda p: (-p["score"], -p["kills"])):
                put(scr, row, 0, "   %-16s %d win(s)  %d kill(s)" % (p["name"][:16], p["score"], p["kills"]))
                row += 1
        row += 1
        put(scr, row, 0, "r: ready for the next round   any key: lobby   q: quit")

    # ---- main loop ----

    def run(self):
        scr = self.scr
        scr.timeout(20)
        scr.keypad(True)
        curses.curs_set(0)
        init_colors()
        self.conn.send("HELLO %d %s" % (PROTO, self.my_name))
        while not self.quit:
            got = False
            while True:
                try:
                    m = self.conn.q.get_nowait()
                except queue.Empty:
                    break
                if m is None:
                    self.msg = "disconnected from the server. q: quit"
                    self.dirty = True
                    break
                self.handle(m)
                got = True
            if self.conn.closed and not got and self.screen != "result":
                self.dirty = True
            if self.dirty:
                self.draw()
                self.dirty = False
            c = scr.getch()
            if c != -1:
                self.key(c)
                if self.screen != "game":
                    self.dirty = True
        self.conn.send("BYE")


def main():
    opts = parse_args(sys.argv)
    locale.setlocale(locale.LC_ALL, "")
    if not opts["ascii"]:
        enc = (locale.getpreferredencoding(False) or "").lower()
        if "utf" not in enc:
            opts["ascii"] = True
    host = opts["host"] or "127.0.0.1"
    try:
        conn = Connection(host, opts["port"])
    except OSError as e:
        sys.exit("cannot connect to %s:%d (%s)" % (host, opts["port"], e))
    client = Client(None, conn, opts)

    def wrapped(scr):
        client.scr = scr
        client.run()

    try:
        curses.wrapper(wrapped)
    finally:
        conn.close()
    r = client.result
    if r:
        print("ROUND %d RESULT" % r["round"])
        for p in r["ranking"]:
            print("%d. %s (%s, kills %d)" % (p["rank"], p["name"], p["cause"], p["kills"]))


if __name__ == "__main__":
    main()
