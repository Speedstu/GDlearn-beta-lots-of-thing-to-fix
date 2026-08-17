#!/usr/bin/env python3
"""Download real Geometry Dash levels from the live servers and convert to .gdl.

Uses the current 2.2 transport shape: HTTPS, the 2.2 gd cookie/Host headers,
gameVersion 22 and binaryVersion 42.  This matters on hosted CI runners: the
old 2.1 HTTP shape is routinely rejected before the request reaches the game
endpoint.

Examples:
  python tools/fetch_gd_level.py --search "Bloodbath"
  python tools/fetch_gd_level.py --id 10565740 --out levels_real
  python tools/fetch_gd_level.py --pack demons --out levels_real
"""
import argparse
import os
import sys
import urllib.parse
import urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gmd_to_gdl import convert, try_decompress  # noqa: E402

BASE = "https://www.boomlings.com/database"
SECRET = "Wmfd2893gb7"
GAME_VERSION = "22"
BINARY_VERSION = "42"

PACKS = {
    "demons": [
        "Bloodbath", "Cataclysm", "Sonic Wave", "Yatagarasu",
        "Ice Carbon Diablo X", "Nine Circles", "The Nightmare",
        "Death Moon", "Windy Landscape", "Erebus",
    ],
    "classics": [
        "Theory of Everything 2", "Clubstep", "Deadlocked", "Electrodynamix",
        "Clutterfunk", "Hexagon Force", "Blast Processing",
    ],
}


def post(endpoint, params):
    """POST like a current 2.2 client, without a browser User-Agent."""
    data = urllib.parse.urlencode(params).encode("ascii")
    req = urllib.request.Request(f"{BASE}/{endpoint}", data=data, method="POST")
    req.add_header("User-Agent", "")
    req.add_header("Accept", "*/*")
    req.add_header("Content-Type", "application/x-www-form-urlencoded")
    req.add_header("Cookie", "gd=1;")
    req.add_header("Host", "www.boomlings.com")
    with urllib.request.urlopen(req, timeout=30) as fh:
        return fh.read().decode("utf-8", "ignore")


def parse_kv(chunk, sep=":"):
    """GD responses are flat key:value:key:value lists."""
    parts = chunk.split(sep)
    return {parts[i]: parts[i + 1] for i in range(0, len(parts) - 1, 2)}


def search(name, page=0):
    body = post("getGJLevels21.php", {
        "secret": SECRET,
        "type": "0",
        "str": name,
        "page": str(page),
        "gameVersion": GAME_VERSION,
        "binaryVersion": BINARY_VERSION,
    })
    if not body or body.strip() == "-1":
        return []
    levels = body.split("#")[0].split("|")
    out = []
    for chunk in levels:
        kv = parse_kv(chunk)
        if "1" not in kv:
            continue
        out.append({
            "id": kv.get("1", ""),
            "name": kv.get("2", ""),
            "downloads": int(kv.get("10", 0) or 0),
            "likes": int(kv.get("14", 0) or 0),
            "demon": kv.get("17", "") == "1",
            "stars": int(kv.get("18", 0) or 0),
        })
    return out


def download(level_id):
    # downloadGJLevel22 only needs levelID + common secret for public levels.
    # Version fields are harmless and keep the request shape explicit/current.
    body = post("downloadGJLevel22.php", {
        "secret": SECRET,
        "levelID": str(level_id),
        "gameVersion": GAME_VERSION,
        "binaryVersion": BINARY_VERSION,
    })
    if not body or body.strip() == "-1":
        raise RuntimeError(f"server refused level {level_id} (deleted, or rate limited)")
    kv = parse_kv(body.split("#")[0])
    name = kv.get("2") or f"level_{level_id}"
    raw = kv.get("4")
    if not raw:
        raise RuntimeError(f"no level data in the response for {level_id}")
    data = try_decompress(raw)
    if not data:
        raise RuntimeError(f"could not decode the level string for {level_id}")
    return name, data.decode("utf-8", "ignore")


def save(level_id, name, level_string, out_dir):
    text, count = convert(level_string, name)
    os.makedirs(out_dir, exist_ok=True)
    dst = os.path.join(out_dir, f"{level_id}.gdl")
    with open(dst, "w", encoding="utf-8") as fh:
        fh.write(text)
    print(f"  {name!r} (id {level_id}) -> {dst}  ({count} gameplay objects)")
    return dst


def best_match(name):
    hits = search(name)
    if not hits:
        return None
    exact = [h for h in hits if h["name"].strip().lower() == name.strip().lower()]
    pool = exact or hits
    return max(pool, key=lambda h: h["downloads"])


def main():
    ap = argparse.ArgumentParser(description="Fetch GD levels as .gdl")
    ap.add_argument("--id", action="append", default=[], help="level ID (repeatable)")
    ap.add_argument("--search", help="list matches for a name, download nothing")
    ap.add_argument("--name", action="append", default=[],
                    help="resolve a name to its most downloaded level and fetch it")
    ap.add_argument("--pack", choices=sorted(PACKS), help="fetch a curated set")
    ap.add_argument("--out", default="levels_real")
    args = ap.parse_args()

    if args.search:
        hits = search(args.search)
        if not hits:
            print("no match")
            return 1
        print(f"{'id':<12}{'stars':<7}{'likes':<10}{'downloads':<12}name")
        for h in sorted(hits, key=lambda h: -h["downloads"]):
            tag = " [demon]" if h["demon"] else ""
            print(f"{h['id']:<12}{h['stars']:<7}{h['likes']:<10}{h['downloads']:<12}{h['name']}{tag}")
        return 0

    names = list(args.name) + (PACKS[args.pack] if args.pack else [])
    ids = list(args.id)
    ok = fail = 0
    for name in names:
        try:
            hit = best_match(name)
            if not hit:
                print(f"  {name}: no match on the servers")
                fail += 1
                continue
            print(f"  resolved {name!r} -> {hit['name']!r} id {hit['id']} "
                  f"({hit['downloads']} downloads)")
            ids.append(hit["id"])
        except Exception as exc:
            print(f"  {name}: search failed ({exc})")
            fail += 1
    for level_id in ids:
        try:
            name, level_string = download(level_id)
            save(level_id, name, level_string, args.out)
            ok += 1
        except Exception as exc:
            print(f"  {level_id}: {exc}")
            fail += 1
    print(f"fetched {ok} level(s), {fail} failure(s) -> {args.out}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
