#!/usr/bin/env python3
"""BombConsole bot (protocol v2). Python 3 standard library only.

usage: python3 bot.py [host[:port]] [--name NAME] [--rounds N] [--quiet]

Connects, says READY, and plays simple rules each tick:
  1. if my square is in danger (a bomb's fire reaches it), run to a safe square
  2. else if an item is reachable, go for it
  3. else if I stand next to a soft block (or an enemy) and have a bomb, drop it
  4. else wander towards the nearest soft block / enemy
It exits after N rounds (default 1) and prints the result as JSON on stdout.
It is also a small example of a client without curses.
"""
import json
import os
import random
import socket
import sys
from collections import deque

PROTO = 1
DEFAULT_PORT = 10001
DIRS = {"U": (0, -1), "D": (0, 1), "L": (-1, 0), "R": (1, 0)}


def parse_args(argv):
    o = {"host": "127.0.0.1", "port": DEFAULT_PORT, "name": "bot", "rounds": 1, "quiet": False, "seed": None}
    env = os.environ.get("BOMBCONSOLE_SERVER")
    if env:
        o["host"] = env
    i = 1
    while i < len(argv):
        a = argv[i]
        if a == "--name":
            o["name"] = argv[i + 1]
            i += 1
        elif a == "--rounds":
            o["rounds"] = int(argv[i + 1])
            i += 1
        elif a == "--seed":
            o["seed"] = int(argv[i + 1])
            i += 1
        elif a == "--quiet":
            o["quiet"] = True
        elif a.startswith("-"):
            sys.exit("unknown option: " + a)
        else:
            o["host"] = a
        i += 1
    if ":" in o["host"]:
        h, p = o["host"].rsplit(":", 1)
        o["host"], o["port"] = h, int(p)
    return o


def blast_cells(st, bomb, fire_len):
    """Squares reached by the fire of bomb (same rule as the server)."""
    m = st["map"]
    w, h = len(m[0]), len(m)
    cells = {(bomb["x"], bomb["y"])}
    for dx, dy in DIRS.values():
        for r in range(1, fire_len + 1):
            x, y = bomb["x"] + dx * r, bomb["y"] + dy * r
            if not (0 <= x < w and 0 <= y < h) or m[y][x] == "#":
                break
            cells.add((x, y))
            if m[y][x] in "+FBS" or (x, y) in {(b["x"], b["y"]) for b in st["bombs"]}:
                break
    return cells


class Bot:
    def __init__(self, opts):
        self.o = opts
        self.me = None
        self.rng = random.Random(opts["seed"])
        self.fire = {}      # player id -> fire length

    def danger(self, st):
        d = set()
        for b in st["bombs"]:
            d |= blast_cells(st, b, self.fire.get(b["owner"], 2))
        for y, row in enumerate(st["map"]):
            for x, ch in enumerate(row):
                if ch == "*":
                    d.add((x, y))
        return d

    def walkable(self, st, x, y):
        m = st["map"]
        if not (0 <= y < len(m) and 0 <= x < len(m[0])):
            return False
        ch = m[y][x]
        return ch not in "#+o*"

    def bfs(self, st, start, goal_fn, avoid):
        """First step (direction) of the shortest path to a square where goal_fn is true."""
        prev = {start: None}
        q = deque([start])
        while q:
            cur = q.popleft()
            if cur != start and goal_fn(cur):
                while prev[cur] != start:
                    cur = prev[cur]
                dx, dy = cur[0] - start[0], cur[1] - start[1]
                for k, v in DIRS.items():
                    if v == (dx, dy):
                        return k
            for dx, dy in DIRS.values():
                nxt = (cur[0] + dx, cur[1] + dy)
                if nxt in prev or not self.walkable(st, nxt[0], nxt[1]) or nxt in avoid:
                    continue
                prev[nxt] = cur
                q.append(nxt)
        return None

    def decide(self, st):
        me = None
        for p in st["players"]:
            self.fire[p["id"]] = p["fire"]
            if p["id"] == self.me:
                me = p
        if me is None or me["st"] != "alive":
            return None
        pos = (me["x"], me["y"])
        danger = self.danger(st)
        m = st["map"]
        enemies = {(p["x"], p["y"]) for p in st["players"] if p["id"] != self.me and p["st"] == "alive"}

        if pos in danger:
            d = self.bfs(st, pos, lambda c: c not in danger, set())
            return (d or self.rng.choice(list(DIRS)), 0)

        def is_item(c):
            return m[c[1]][c[0]] in "FBS"
        d = self.bfs(st, pos, is_item, danger)
        if d:
            return (d, 0)

        def next_to_target(c):
            for dx, dy in DIRS.values():
                nx, ny = c[0] + dx, c[1] + dy
                if (nx, ny) in enemies or (0 <= ny < len(m) and 0 <= nx < len(m[0]) and m[ny][nx] == "+"):
                    return True
            return False
        # attack: an enemy (or a soft block) is within my blast
        if me["bombs"] > 0:
            mine = {"x": pos[0], "y": pos[1], "owner": self.me}
            blast = blast_cells(st, mine, me["fire"])
            hit = any(c in enemies for c in blast) or next_to_target(pos)
            if hit:
                # only drop if there is an escape afterwards
                fake = dict(st)
                fake["bombs"] = st["bombs"] + [mine]
                d2 = self.danger(fake)
                if self.bfs(fake, pos, lambda c: c not in d2, set()):
                    return ("-", 1)
        d = self.bfs(st, pos, next_to_target, danger)
        if d and self.rng.random() < 0.8:
            return (d, 0)
        if self.rng.random() < 0.5:
            d = self.rng.choice(list(DIRS))
            dx, dy = DIRS[d]
            if self.walkable(st, pos[0] + dx, pos[1] + dy) and (pos[0] + dx, pos[1] + dy) not in danger:
                return (d, 0)
        return None

    def run(self):
        o = self.o
        s = socket.create_connection((o["host"], o["port"]))
        s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        f = s.makefile("r", encoding="utf-8")
        s.sendall(("HELLO %d %s\n" % (PROTO, o["name"])).encode())
        s.sendall(b"READY 1\n")
        rounds_done = 0
        last_tick_sent = -1
        for line in f:
            try:
                m = json.loads(line)
            except ValueError:
                continue
            t = m.get("t")
            if t == "welcome":
                self.me = m["you"]
                if not o["quiet"]:
                    print("joined as %s (#%d)" % (m["name"], self.me + 1), file=sys.stderr)
            elif t == "lobby":
                for p in m["players"]:
                    if p["id"] == self.me and not p["ready"]:
                        s.sendall(b"READY 1\n")
            elif t == "start":
                if not o["quiet"]:
                    print("round %d: %dx%d" % (m["round"], m["w"], m["h"]), file=sys.stderr)
            elif t == "state":
                # decide only every other tick: one step takes several ticks anyway
                if m["tick"] - last_tick_sent < 2:
                    continue
                try:
                    act = self.decide(m)
                except Exception:
                    import traceback
                    traceback.print_exc()
                    act = None
                if act:
                    last_tick_sent = m["tick"]
                    s.sendall(("IN %s %d\n" % act).encode())
            elif t == "result":
                rounds_done += 1
                print(json.dumps(m, ensure_ascii=False))
                sys.stdout.flush()
                if rounds_done >= o["rounds"]:
                    break
            elif t == "end" and "score" not in m:
                break
        s.sendall(b"BYE\n")
        s.close()


if __name__ == "__main__":
    Bot(parse_args(sys.argv)).run()
