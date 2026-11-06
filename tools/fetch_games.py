#!/usr/bin/env python3
"""Download a player's games from Lichess or Chess.com into one PGN file.

The app (and the player-book tool) learn the player's moves from this file.

    python3 tools/fetch_games.py lichess DrNykterstein
    python3 tools/fetch_games.py chesscom hikaru --max 2000 --speed blitz,rapid
    python3 tools/fetch_games.py lichess someone --since 2024-01-01 --out my-games.pgn

Only the Python standard library is needed. Games are saved to players/<user>-<site>.pgn
by default, which is where the app looks for them.
"""

from __future__ import annotations

import argparse
import datetime as dt
import json
import os
import re
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path
from typing import Iterable, Iterator

USER_AGENT = "chess-next-move-predictor (+https://github.com/anurag67-droid/chess-next-move-predictor)"
SPEEDS = ("bullet", "blitz", "rapid", "classical", "correspondence")
PLAYERS_DIR = Path(__file__).resolve().parent.parent / "players"


class FetchError(Exception):
    pass


# ---------------------------------------------------------------------------
# HTTP


def open_url(url: str, headers: dict[str, str] | None = None, retries: int = 4):
    """Opens a URL, waiting and retrying when the site asks us to slow down."""
    request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT, **(headers or {})})
    for attempt in range(retries + 1):
        try:
            return urllib.request.urlopen(request, timeout=60)
        except urllib.error.HTTPError as e:
            if e.code == 404:
                raise FetchError("not found (check the username): " + url) from None
            if e.code == 429 and attempt < retries:
                wait = int(e.headers.get("Retry-After") or 60)
                print(f"  rate limited, waiting {wait} s ...", file=sys.stderr)
                time.sleep(wait)
                continue
            if e.code >= 500 and attempt < retries:
                time.sleep(2 ** (attempt + 1))
                continue
            raise FetchError(f"HTTP {e.code} from {url}") from None
        except urllib.error.URLError as e:
            if attempt < retries:
                time.sleep(2 ** (attempt + 1))
                continue
            raise FetchError(f"could not reach {url}: {e.reason}") from None
    raise FetchError("too many retries: " + url)


# ---------------------------------------------------------------------------
# PGN helpers


def split_games(lines: Iterable[str]) -> Iterator[str]:
    """Splits PGN text into games (each game starts with its first header after the moves)."""
    game: list[str] = []
    in_moves = False
    for line in lines:
        stripped = line.strip()
        if stripped.startswith("[") and in_moves:
            yield "".join(game).strip() + "\n"
            game, in_moves = [], False
        if stripped and not stripped.startswith("["):
            in_moves = True
        game.append(line if line.endswith("\n") else line + "\n")
    if any(l.strip() for l in game):
        yield "".join(game).strip() + "\n"


HEADER_RE = re.compile(r'^\[(\w+)\s+"(.*)"\]\s*$', re.MULTILINE)


def headers(game: str) -> dict[str, str]:
    return dict(HEADER_RE.findall(game))


def speed_of(time_control: str) -> str:
    """Lichess' speed categories (estimated duration = base + 40 * increment)."""
    if time_control == "-" or "/" in time_control:
        return "correspondence"
    try:
        base, _, inc = time_control.partition("+")
        estimate = int(base) + 40 * int(inc or 0)
    except ValueError:
        return "unknown"
    if estimate < 180:
        return "bullet"
    if estimate < 480:
        return "blitz"
    if estimate < 1500:
        return "rapid"
    return "classical"


def is_standard(tags: dict[str, str]) -> bool:
    return tags.get("Variant", "Standard").lower() in ("standard", "from position")


# ---------------------------------------------------------------------------
# Sites


def lichess_games(user: str, args) -> Iterator[str]:
    params = {"clocks": "true", "opening": "true"}
    # perfType also keeps out variants such as chess960. Ultrabullet counts as bullet here.
    perf_types = list(args.speed)
    if "bullet" in perf_types:
        perf_types.insert(0, "ultraBullet")
    params["perfType"] = ",".join(perf_types)
    if args.max:
        params["max"] = str(args.max)
    if args.rated_only:
        params["rated"] = "true"
    if args.since:
        params["since"] = str(int(args.since.timestamp() * 1000))
    if args.until:
        params["until"] = str(int(args.until.timestamp() * 1000))

    url = f"https://lichess.org/api/games/user/{urllib.parse.quote(user)}?{urllib.parse.urlencode(params)}"
    request_headers = {"Accept": "application/x-chess-pgn"}
    token = args.token or os.environ.get("LICHESS_TOKEN")
    if token:
        request_headers["Authorization"] = "Bearer " + token

    with open_url(url, request_headers) as response:
        lines = (raw.decode("utf-8", errors="replace") for raw in response)
        yield from split_games(lines)


def chesscom_games(user: str, args) -> Iterator[str]:
    base = f"https://api.chess.com/pub/player/{urllib.parse.quote(user.lower())}"
    with open_url(base + "/games/archives") as response:
        archives: list[str] = json.load(response).get("archives", [])
    if not archives:
        raise FetchError(f"{user} has no games on Chess.com")

    def month_of(url: str) -> dt.datetime:
        year, month = url.rstrip("/").split("/")[-2:]
        return dt.datetime(int(year), int(month), 1, tzinfo=dt.timezone.utc)

    if args.since:
        first = args.since.replace(day=1, hour=0, minute=0, second=0, microsecond=0)
        archives = [a for a in archives if month_of(a) >= first]
    if args.until:
        archives = [a for a in archives if month_of(a) <= args.until]

    # Newest month first, so --max keeps the most recent games.
    for archive in reversed(archives):
        print(f"  {month_of(archive):%Y-%m} ...", file=sys.stderr)
        with open_url(archive + "/pgn") as response:
            text = response.read().decode("utf-8", errors="replace")
        month = list(split_games(text.splitlines(keepends=True)))
        yield from reversed(month)  # newest game first within the month too


# ---------------------------------------------------------------------------


def parse_date(text: str) -> dt.datetime:
    for fmt in ("%Y-%m-%d", "%Y-%m"):
        try:
            return dt.datetime.strptime(text, fmt).replace(tzinfo=dt.timezone.utc)
        except ValueError:
            pass
    raise argparse.ArgumentTypeError(f"expected YYYY-MM-DD or YYYY-MM, got {text!r}")


def parse_speeds(text: str) -> tuple[str, ...]:
    speeds = tuple(s.strip().lower() for s in text.split(",") if s.strip())
    unknown = [s for s in speeds if s not in SPEEDS]
    if unknown:
        raise argparse.ArgumentTypeError(f"unknown speed(s) {', '.join(unknown)}; choose from {', '.join(SPEEDS)}")
    return speeds


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("site", choices=("lichess", "chesscom"), help="where the player's games are")
    parser.add_argument("user", help="the player's username on that site")
    parser.add_argument("--max", type=int, default=0, help="keep at most this many of the newest games")
    parser.add_argument("--speed", type=parse_speeds, default=SPEEDS,
                        help="comma-separated speeds to keep (default: all): " + ",".join(SPEEDS))
    parser.add_argument("--since", type=parse_date, help="only games from this date on (YYYY-MM-DD)")
    parser.add_argument("--until", type=parse_date, help="only games up to this date (YYYY-MM-DD)")
    parser.add_argument("--rated-only", action="store_true", help="skip casual games (Lichess only)")
    parser.add_argument("--token", help="Lichess API token for faster downloads (or set LICHESS_TOKEN)")
    parser.add_argument("--out", type=Path, help="output file (default: players/<user>-<site>.pgn)")
    args = parser.parse_args(argv)

    out: Path = args.out or PLAYERS_DIR / f"{args.user.lower()}-{args.site}.pgn"
    out.parent.mkdir(parents=True, exist_ok=True)
    source = lichess_games if args.site == "lichess" else chesscom_games

    print(f"Downloading {args.user}'s games from {'Lichess' if args.site == 'lichess' else 'Chess.com'} ...",
          file=sys.stderr)
    kept: list[str] = []
    skipped = 0
    started = time.monotonic()
    try:
        for game in source(args.user, args):
            tags = headers(game)
            # Lichess filters on its side; Chess.com needs it done here.
            if not is_standard(tags) or speed_of(tags.get("TimeControl", "")) not in args.speed:
                skipped += 1
                continue
            kept.append(game)
            if len(kept) % 500 == 0:
                print(f"  {len(kept)} games", file=sys.stderr)
            if args.max and len(kept) >= args.max:
                break
    except KeyboardInterrupt:
        print("Interrupted; saving the games downloaded so far.", file=sys.stderr)
    except FetchError as e:
        print(f"error: {e}", file=sys.stderr)
        if not kept:
            return 1

    if not kept:
        print("No games matched.", file=sys.stderr)
        return 1

    # Write via a temporary file so an interrupted run never leaves half a PGN behind.
    temp = out.with_suffix(out.suffix + ".part")
    temp.write_text("\n".join(kept), encoding="utf-8")
    temp.replace(out)

    seconds = time.monotonic() - started
    note = f" ({skipped} variant or other-speed games skipped)" if skipped else ""
    print(f"Saved {len(kept)} games to {out}{note} in {seconds:.0f} s", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
