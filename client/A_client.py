#!/usr/bin/env python3
"""BombConsole client (protocol v2). Python 3 standard library only.

usage: python3 A_client.py [host[:port]] [--name NAME] [--ascii] [--bots N]
       python3 A_client.py --host [--players N] [--bots N] [-- server options...]

  host      server address. Without it the client looks for servers on the LAN
            (UDP broadcast) and connects to the one it finds; $BOMBCONSOLE_SERVER=host:port
            also works where broadcasts don't (VPN, guest Wi-Fi)
  --host    start a server (server/A_server --no-ui) as a child process and join it.
            Needs the repository (builds the server with make if necessary)
  --bots N  also start N bots (client/bot.py) and bring them to the same server
  --players N  humans expected including you (--host only, default 1). The server
            starts automatically when players + bots have joined; if that is below 2,
            the round starts with r as usual
  --        everything after it goes to the server, e.g. -- --time 90 --seed 3
  --name    player name (default: $BOMBCONSOLE_NAME, then the login user name)
  --ascii   draw with ASCII characters only (2 columns per square)
  --no-threat  start with the danger highlight off (t toggles it)

examples:
  python3 A_client.py --host --bots 2              # play alone against 2 bots
  python3 A_client.py --host --players 3 --bots 1  # host for 3 humans + 1 bot
  python3 A_client.py                              # join a server on the LAN

keys: arrows / WASD / hjkl = move, Space or z = bomb, r = ready, q = quit,
      t = toggle the danger highlight (squares the current bombs will reach),
      g = force start (when hosting)
"""
import curses
import getpass
import json
import locale
import os
import queue
import re
import socket
import subprocess
import sys
import threading
import time

PROTO = 1
DEFAULT_PORT = 10001
DOUBLE_Q_SEC = 1.0      # press q twice within this time to quit during a round

# ---------------------------------------------------------------- arguments


def parse_args(argv):
    opts = {"host": None, "port": DEFAULT_PORT, "name": None, "ascii": False, "threat": True,
            "hosting": False, "players": None, "bots": 0, "server_args": []}
    env = os.environ.get("BOMBCONSOLE_SERVER")
    if env:
        opts["host"] = env
    i = 1
    while i < len(argv):
        a = argv[i]
        if a == "--":
            opts["server_args"] = argv[i + 1:]
            break
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
        elif a == "--no-threat":
            opts["threat"] = False
        elif a == "--host":
            opts["hosting"] = True
        elif a == "--bots" and i + 1 < len(argv):
            opts["bots"] = int(argv[i + 1])
            i += 1
        elif a.startswith("--bots="):
            opts["bots"] = int(a.split("=", 1)[1])
        elif a == "--players" and i + 1 < len(argv):
            opts["players"] = int(argv[i + 1])
            i += 1
        elif a.startswith("--players="):
            opts["players"] = int(a.split("=", 1)[1])
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
    if opts["bots"] < 0:
        opts["bots"] = 0
    if opts["players"] is not None and not opts["hosting"]:
        print("warning: --players only matters with --host; ignored", file=sys.stderr)
        opts["players"] = None
    if opts["server_args"] and not opts["hosting"]:
        print("warning: server options after -- only matter with --host; ignored", file=sys.stderr)
        opts["server_args"] = []
    if opts["hosting"]:
        if opts["players"] is None:
            opts["players"] = 1
        if opts["players"] < 1:
            opts["players"] = 1
        # the server must bind the port we connect to
        for k, a in enumerate(opts["server_args"]):
            if a == "--port" and k + 1 < len(opts["server_args"]):
                opts["port"] = int(opts["server_args"][k + 1])
            elif a.startswith("--port="):
                opts["port"] = int(a.split("=", 1)[1])
        if opts["host"]:
            print("warning: --host starts a local server; the address %s is ignored" % opts["host"], file=sys.stderr)
        opts["host"] = "127.0.0.1"
    return opts


# ---------------------------------------------------------------- hosting (child processes)

SERVER_LOG = ".bombconsole-server.log"


class Host:
    """Runs server/A_server --no-ui and client/bot.py as child processes and stops them at exit."""

    def __init__(self):
        self.root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
        self.server = None
        self.bots = []
        self.port = None
        self.log_path = None
        self.addresses = []     # LAN addresses printed by the server
        self.autostart = 0

    def _spawn(self, args, **kw):
        kw.setdefault("stdin", subprocess.DEVNULL)
        kw.setdefault("stdout", subprocess.DEVNULL)
        kw.setdefault("stderr", subprocess.STDOUT)
        kw.setdefault("start_new_session", True)    # Ctrl-C reaches only us; we stop them ourselves
        return subprocess.Popen(args, **kw)

    def server_binary(self):
        exe = os.path.join(self.root, "server", "A_server")
        if os.path.exists(exe):
            return exe
        if not os.path.exists(os.path.join(self.root, "server", "makefile")):
            sys.exit("--host needs the repository (server/ next to client/): %s not found\n"
                     "get it with: git clone <BombConsole repository>" % exe)
        print("building the server (make)...")
        try:
            r = subprocess.call(["make"], cwd=self.root)
        except OSError as e:
            sys.exit("cannot run make (%s). Install g++ and make, or start server/A_server yourself" % e)
        if r != 0 or not os.path.exists(exe):
            sys.exit("the server did not build. Check g++ / make, then run `make` in %s" % self.root)
        return exe

    def start_server(self, port, server_args, autostart):
        exe = self.server_binary()
        self.log_path = os.path.join(self.root, SERVER_LOG)
        self.autostart = autostart
        args = [exe, "--no-ui", "--port", str(port)]
        if autostart >= 2:
            args += ["--autostart", str(autostart)]
        args += server_args
        log = open(self.log_path, "w")
        try:
            self.server = self._spawn(args, stdin=subprocess.PIPE, stdout=log, stderr=subprocess.STDOUT)
        finally:
            log.close()
        # wait for "listening on TCP port N" (and collect the "clients: ..." hints)
        deadline = time.time() + 15
        while time.time() < deadline:
            if self.server.poll() is not None:
                break
            text = self.read_log()
            m = re.search(r"listening on TCP port (\d+)", text)
            if m and "clients:" in text:
                self.port = int(m.group(1))
                time.sleep(0.1)
                self.addresses = re.findall(r"clients: python3 A_client.py (\S+)", self.read_log())
                return self.port
            time.sleep(0.1)
        self.stop()
        sys.exit("the server did not start. Its output (%s):\n%s" % (self.log_path, self.read_log()[-2000:]))

    def read_log(self):
        try:
            with open(self.log_path, encoding="utf-8", errors="replace") as f:
                return f.read()
        except OSError:
            return ""

    def start_bots(self, host, port, n):
        bot = os.path.join(self.root, "client", "bot.py")
        if n > 0 and not os.path.exists(bot):
            print("warning: %s not found, no bots started" % bot, file=sys.stderr)
            return
        for i in range(n):
            self.bots.append(self._spawn([sys.executable, bot, "%s:%d" % (host, port), "--name", "bot%d" % (i + 1),
                                          "--rounds", "0", "--quiet", "--seed", str(i + 1)]))

    def force_start(self):
        """Send a 'g' line to the server's stdin (same as pressing g on the server screen)."""
        if self.server is None or self.server.stdin is None or self.server.poll() is not None:
            return False
        try:
            self.server.stdin.write(b"g\n")
            self.server.stdin.flush()
            return True
        except OSError:
            return False

    def server_alive(self):
        return self.server is not None and self.server.poll() is None

    def stop(self):
        procs = [p for p in self.bots + [self.server] if p is not None]
        for p in procs:
            if p.poll() is None:
                try:
                    p.terminate()
                except OSError:
                    pass
        deadline = time.time() + 3
        for p in procs:
            while p.poll() is None and time.time() < deadline:
                time.sleep(0.05)
            if p.poll() is None:
                try:
                    p.kill()
                except OSError:
                    pass
                try:
                    p.wait(timeout=2)
                except (OSError, subprocess.TimeoutExpired):
                    pass
        if self.server is not None and self.server.stdin:
            try:
                self.server.stdin.close()
            except OSError:
                pass
        self.bots = []
        self.server = None


# ---------------------------------------------------------------- discovery


def discover(port=DEFAULT_PORT, wait=1.0):
    """Broadcast BOMBCONSOLE? and collect (host, tcp_port, name) replies for `wait` seconds."""
    found = []
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        s.settimeout(0.2)
        for _ in range(2):
            try:
                s.sendto(b"BOMBCONSOLE?", ("255.255.255.255", port))
                s.sendto(b"BOMBCONSOLE?", ("127.0.0.1", port))
            except OSError:
                pass
            end = time.time() + wait / 2
            while time.time() < end:
                try:
                    data, addr = s.recvfrom(256)
                except socket.timeout:
                    continue
                except OSError:
                    break
                parts = data.decode("utf-8", "replace").split(" ", 2)
                if len(parts) >= 2 and parts[0] == "BOMBCONSOLE":
                    entry = (addr[0], int(parts[1]), parts[2] if len(parts) > 2 else "")
                    # the same server answers on 127.0.0.1 and on its LAN address: keep one
                    dup = [e for e in found if e[1:] == entry[1:]]
                    if not dup:
                        found.append(entry)
                    elif dup[0][0].startswith("127.") and not entry[0].startswith("127."):
                        found[found.index(dup[0])] = entry
        s.close()
    except OSError:
        pass
    return found


def choose_server(port):
    print("looking for BombConsole servers on the LAN...")
    found = discover(port)
    if len(found) == 1:
        host, tcp_port, name = found[0]
        print("found %s at %s:%d" % (name, host, tcp_port))
        return host, tcp_port
    if len(found) > 1:
        for i, (host, tcp_port, name) in enumerate(found):
            print("  %d) %s  %s:%d" % (i + 1, name, host, tcp_port))
        while True:
            ans = input("which server? [1] ").strip() or "1"
            if ans.isdigit() and 1 <= int(ans) <= len(found):
                host, tcp_port, _ = found[int(ans) - 1]
                return host, tcp_port
    ans = input("no server found. address (host[:port], empty = 127.0.0.1): ").strip() or "127.0.0.1"
    if ":" in ans:
        h, p = ans.rsplit(":", 1)
        return h, int(p)
    return ans, port


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

C_WALL, C_SOFT, C_BOMB, C_FIRE, C_ITEM, C_DYING, C_THREAT, C_HUD, C_BOMB_SOON = range(1, 10)
C_PLAYER = 10   # + player id (0..7)
C_PLAYER_THREAT = 20    # + player id, on a threatened square


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
    curses.init_pair(C_THREAT, curses.COLOR_YELLOW, curses.COLOR_RED)
    curses.init_pair(C_HUD, curses.COLOR_CYAN, bg)
    curses.init_pair(C_BOMB_SOON, curses.COLOR_RED, bg)
    pc = [curses.COLOR_CYAN, curses.COLOR_GREEN, curses.COLOR_YELLOW, curses.COLOR_MAGENTA,
          curses.COLOR_RED, curses.COLOR_BLUE, curses.COLOR_WHITE, curses.COLOR_CYAN]
    for i in range(8):
        curses.init_pair(C_PLAYER + i, pc[i], bg)
        curses.init_pair(C_PLAYER_THREAT + i, pc[i], curses.COLOR_RED)


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
        self.show_threat = opts["threat"]
        self.host = None            # Host when --host

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
            elif c in (ord("g"), ord("G")) and self.host:
                n = len(self.lobby["players"]) if self.lobby else 0
                if n >= 2:
                    self.msg = "force start sent" if self.host.force_start() else "cannot reach the server"
                else:
                    self.msg = "need 2 or more players to start"
                self.dirty = True
            return
        # game
        if c in self.DIRS:
            self.conn.send("IN %s 0" % self.DIRS[c])
        elif c in (ord(" "), ord("z"), ord("Z")):
            self.conn.send("IN - 1")
        elif c in (ord("t"), ord("T")):
            self.show_threat = not self.show_threat
            self.msg = "danger highlight %s" % ("on" if self.show_threat else "off")
            self.dirty = True

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
        put(scr, 1, 0, "r: ready / not ready    q: quit" + ("    g: force start" if self.host else ""))
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
        if self.host:
            row += 1
            put(scr, row, 0, "hosting (server log: %s)" % os.path.basename(self.host.log_path or ""), attr(C_HUD))
            row += 1
            if self.host.autostart >= 2:
                put(scr, row, 0, "  starts automatically when %d have joined (humans %d + bots %d)" % (
                    self.host.autostart, self.opts["players"], self.opts["bots"]))
            else:
                put(scr, row, 0, "  no autostart: everyone presses r (or g here) when all are in")
            row += 1
            for a in self.host.addresses or ["<this host>"]:
                put(scr, row, 0, "  others join with: python3 A_client.py %s" % a, attr(C_ITEM))
                row += 1
            if not self.host.server_alive():
                put(scr, row, 0, "  the server has stopped", attr(C_FIRE))
                row += 1
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
            hud = "%-8s bomb %d/%d fire %d spd %d" % (self.my_name[:8], me["bombs"], me["maxb"], me["fire"], me["spd"])
        else:
            hud = "spectating"
        alive = sum(1 for p in st["players"] if p["st"] == "alive")
        hud += "  alive %d/%d" % (alive, len(st["players"]))
        if st.get("left", -1) >= 0:
            hud += "  time %3d" % int(st["left"] + 0.999)
        if st.get("sd"):
            hud += "  SUDDEN DEATH"
        put(scr, 0, 0, hud[:cols - 1], attr(C_HUD, curses.A_BOLD))
        if st.get("sd"):
            put(scr, 0, hud.index("SUDDEN"), "SUDDEN DEATH", attr(C_FIRE, curses.A_BOLD | curses.A_BLINK))
        # map
        g = self.glyph
        threat = st.get("threat") if self.show_threat else None
        soon = set()
        for b in st.get("bombs", []):
            if b["fuse"] < 1.0:
                soon.add((b["x"], b["y"]))
        for y, line in enumerate(st["map"]):
            trow = threat[y] if threat and y < len(threat) else None
            for x, ch in enumerate(line):
                danger = bool(trow and x < len(trow) and trow[x] == "!")
                text = g.get(ch, ch)
                a = 0
                if ch == "#":
                    a = attr(C_WALL)
                elif ch == "+":
                    a = attr(C_SOFT)
                elif ch == "o":
                    a = attr(C_BOMB_SOON if (x, y) in soon else C_BOMB, curses.A_BOLD)
                elif ch == "*":
                    a = attr(C_FIRE, curses.A_BOLD)
                elif ch == "x":
                    a = attr(C_DYING)
                elif ch in "FBS":
                    a = attr(C_THREAT if danger else C_ITEM, curses.A_BOLD)
                elif "1" <= ch <= "8":
                    pid = ord(ch) - ord("1")
                    base = C_PLAYER_THREAT if danger else C_PLAYER
                    a = attr(base + pid % 8, curses.A_BOLD | (curses.A_REVERSE if pid == self.me else 0))
                elif danger:
                    a = attr(C_THREAT)
                    text = "." if self.opts["ascii"] else "・"
                put(scr, 1 + y, x * self.cell_w, text, a)
        # legend (bottom)
        row = 1 + self.start["h"]
        x = 0
        for p in st["players"]:
            n = self.names.get(p["id"], "?")[:8]
            tag = "" if p["st"] == "alive" else " x"
            text = "%d:%s%s" % (p["id"] + 1, n, tag)
            if x + len(text) >= cols:
                row += 1
                x = 0
            a = attr(C_PLAYER + p["id"] % 8, curses.A_REVERSE if p["id"] == self.me else 0)
            if p["st"] != "alive":
                a = attr(C_DYING)
            put(scr, row, x, text, a)
            x += len(text) + 2
        if me and me["st"] != "alive":
            put(scr, row + 1, 0, "YOU DIED - watching the rest. q: quit", attr(C_FIRE, curses.A_BOLD))
        elif self.msg:
            put(scr, row + 1, 0, self.msg, attr(C_FIRE))
        else:
            put(scr, row + 1, 0, "move: arrows/wasd  bomb: space  t: danger  q: quit"[:cols - 1])

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
        put(scr, row, 0, "r: play again   q: quit   (any other key: back to the lobby)", attr(C_HUD, curses.A_BOLD))
        if self.lobby:
            waiting = [p["name"][:8] for p in self.lobby["players"] if p["ready"]]
            if waiting:
                put(scr, row + 1, 0, "ready for the next round: " + ", ".join(waiting))

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
    host = opts["host"]
    hosting = Host() if opts["hosting"] or opts["bots"] > 0 else None
    if opts["hosting"]:
        total = opts["players"] + opts["bots"]
        hosting.start_server(opts["port"], opts["server_args"], total if total >= 2 else 0)
        opts["port"] = hosting.port
    elif not host:
        try:
            host, opts["port"] = choose_server(opts["port"])
        except (EOFError, KeyboardInterrupt):
            sys.exit("")
    try:
        conn = Connection(host, opts["port"])
    except OSError as e:
        if hosting:
            hosting.stop()
        sys.exit("cannot connect to %s:%d (%s)" % (host, opts["port"], e))
    client = Client(None, conn, opts)
    client.host = hosting if opts["hosting"] else None
    if hosting and opts["bots"] > 0:
        hosting.start_bots(host, opts["port"], opts["bots"])

    def wrapped(scr):
        client.scr = scr
        client.run()

    try:
        curses.wrapper(wrapped)
    finally:
        conn.close()
        if hosting:
            hosting.stop()
    r = client.result
    if r:
        print("ROUND %d RESULT" % r["round"])
        for p in r["ranking"]:
            print("%d. %s (%s, kills %d)" % (p["rank"], p["name"], p["cause"], p["kills"]))
    if hosting and hosting.log_path:
        print("server log: %s" % hosting.log_path)


if __name__ == "__main__":
    main()
