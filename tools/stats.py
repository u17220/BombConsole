#!/usr/bin/env python3
"""Summarise a BombConsole event log (server --log FILE) against the goals in
docs/improvement-plan.md section 1.

usage: python3 tools/stats.py events.jsonl [more.jsonl ...]
"""
import json
import statistics
import sys


def load(paths):
    rounds = []     # list of dicts: start, events, result
    cur = None
    for path in paths:
        with open(path, encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if not line:
                    continue
                try:
                    e = json.loads(line)
                except ValueError:
                    continue
                ev = e.get("ev")
                if ev == "start":
                    cur = {"start": e, "events": [], "result": None}
                    rounds.append(cur)
                elif cur is None:
                    continue
                elif ev == "result":
                    cur["result"] = e
                    cur = None
                else:
                    cur["events"].append(e)
    return [r for r in rounds if r["result"] is not None]


def pct(n, d):
    return "%d/%d (%.0f%%)" % (n, d, 100.0 * n / d) if d else "-"


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__.strip())
    rounds = load(sys.argv[1:])
    if not rounds:
        sys.exit("no complete rounds in the log")
    tick_s = 0.05
    lengths = [r["result"]["ticks"] * tick_s for r in rounds]
    deaths = [p for r in rounds for p in r["result"]["ranking"] if p["cause"] != "alive"]
    by_cause = {}
    for p in deaths:
        by_cause[p["cause"]] = by_cause.get(p["cause"], 0) + 1
    winners = [p for r in rounds for p in r["result"]["ranking"] if p["rank"] == 1]
    seat_wins = {}
    seat_games = {}
    for r in rounds:
        seat_of = {p["id"]: p["seat"] for p in r["start"]["players"]}
        n = len(r["start"]["players"])
        for p in r["result"]["ranking"]:
            seat = (n, seat_of.get(p["id"], -1))
            seat_games[seat] = seat_games.get(seat, 0) + 1
            if p["rank"] == 1:
                seat_wins[seat] = seat_wins.get(seat, 0) + 1
    bombs = sum(1 for r in rounds for e in r["events"] if e["ev"] == "bomb")
    items = sum(1 for r in rounds for e in r["events"] if e["ev"] == "item")
    timeouts = sum(1 for r in rounds if r["result"].get("timeout"))

    print("rounds: %d  (players per round: %s)" % (
        len(rounds), ", ".join(str(len(r["start"]["players"])) for r in rounds[:20]) + (" ..." if len(rounds) > 20 else "")))
    print()
    print("%-44s %-16s %s" % ("goal (improvement-plan section 1)", "target", "measured"))
    print("%-44s %-16s %.1f s (min %.1f, max %.1f)" % ("round length, median", "45..120 s",
                                                          statistics.median(lengths), min(lengths), max(lengths)))
    print("%-44s %-16s %s" % ("rounds decided within 15 s", "< 20%", pct(sum(1 for l in lengths if l < 15), len(lengths))))
    print("%-44s %-16s %s" % ("suicides among deaths", "< 30%", pct(by_cause.get("suicide", 0), len(deaths))))
    print("%-44s %-16s %.1f" % ("items picked up by the winner, mean", ">= 3",
                                  sum(p["items"] for p in winners) / len(winners) if winners else 0))
    if seat_games:
        rates = []
        for seat in sorted(seat_games):
            w = seat_wins.get(seat, 0)
            g = seat_games[seat]
            rates.append((seat, w, g))
        maxr = max((w / g for _, w, g in rates if g), default=0)
        minr = min((w / g for _, w, g in rates if g), default=0)
        print("%-44s %-16s %s" % ("win rate by seat (players, seat): wins/games", "max/min <= 2",
                                  "  ".join("%s %d/%d" % (s, w, g) for s, w, g in rates)))
        if minr > 0:
            print("%-44s %-16s %.2f" % ("  max/min", "<= 2", maxr / minr))
    print()
    print("deaths by cause: %s" % ", ".join("%s %d" % kv for kv in sorted(by_cause.items())))
    print("time-ups: %d   bombs placed: %d (%.1f per round)   items taken: %d (%.1f per round)" % (
        timeouts, bombs, bombs / len(rounds), items, items / len(rounds)))


if __name__ == "__main__":
    main()
