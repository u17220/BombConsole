#!/usr/bin/env python3
"""Headless end-to-end check: start the server with --no-ui on a free port,
connect two bots, and require one full round with a result for both.

usage: python3 tools/check.py [--server PATH] [--bots N] [--timeout SEC] [-- server options...]
"""
import json
import os
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def main():
    server = os.path.join(ROOT, "server", "A_server")
    bots = 2
    timeout = 90.0
    extra = []
    args = sys.argv[1:]
    while args:
        a = args.pop(0)
        if a == "--server":
            server = args.pop(0)
        elif a == "--bots":
            bots = int(args.pop(0))
        elif a == "--timeout":
            timeout = float(args.pop(0))
        elif a == "--":
            extra = args
            break
        else:
            sys.exit("unknown option: " + a)

    srv_args = [server, "--no-ui", "--port", "0", "--countdown", "0", "--seed", "1", "--conf", os.devnull]
    srv_args += extra
    srv = subprocess.Popen(srv_args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                           stdin=subprocess.DEVNULL)
    first = srv.stdout.readline()
    if "port" not in first:
        srv.kill()
        sys.exit("server did not start: " + first + srv.stdout.read())
    port = int(first.strip().split()[-1])
    print("server on port %d" % port)

    procs = []
    for i in range(bots):
        procs.append(subprocess.Popen(
            [sys.executable, os.path.join(ROOT, "client", "bot.py"), "127.0.0.1:%d" % port,
             "--name", "bot%d" % (i + 1), "--seed", str(i + 1)],
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True))

    deadline = time.time() + timeout
    results = []
    ok = True
    for i, p in enumerate(procs):
        remaining = max(0.1, deadline - time.time())
        try:
            out, _ = p.communicate(timeout=remaining)
        except subprocess.TimeoutExpired:
            p.kill()
            out, _ = p.communicate()
            print("bot%d: timeout\n%s" % (i + 1, out))
            ok = False
            continue
        res = None
        for line in out.splitlines():
            if line.startswith("{"):
                try:
                    m = json.loads(line)
                    if m.get("t") == "result":
                        res = m
                except ValueError:
                    pass
        if res is None:
            print("bot%d: no result\n%s" % (i + 1, out))
            ok = False
        else:
            results.append(res)
            print("bot%d: round %d, %d ticks, ranking: %s" % (
                i + 1, res["round"], res["ticks"],
                ", ".join("%d.%s(%s)" % (r["rank"], r["name"], r["cause"]) for r in res["ranking"])))

    srv.terminate()
    try:
        srv_out = srv.communicate(timeout=5)[0]
    except subprocess.TimeoutExpired:
        srv.kill()
        srv_out = srv.communicate()[0]

    if ok and results:
        ranks = [r["ranking"] for r in results]
        if any(r != ranks[0] for r in ranks):
            print("bots received different results")
            ok = False
        if len(ranks[0]) != bots:
            print("expected %d players in the ranking, got %d" % (bots, len(ranks[0])))
            ok = False
    if not ok:
        print("--- server output ---")
        print(srv_out)
        sys.exit("CHECK FAILED")
    print("CHECK OK")


if __name__ == "__main__":
    main()
