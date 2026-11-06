"""Builds a training set from human games: python -m chessml.prepare --help

Streams PGN (a local file, a .pgn.zst archive, or a Lichess database URL), keeps the games that pass
the filters, and writes every position as a compact record into numpy shards:

    <out>/train_00000.npy, train_00001.npy, ...   (99% of the games)
    <out>/val_00000.npy, ...                      (every 100th game, never trained on)
    <out>/meta.json                               (counts and the options used)

Only the cheap header check runs in the main process; the move parsing runs on all CPU cores.
"""

from __future__ import annotations

import argparse
import json
import multiprocessing as mp
import sys
import time
from pathlib import Path

import numpy as np

from .encoding import ELO_STEP, RECORD, speed_of
from .pgn_data import RawGame, game_positions, is_standard, iter_raw_games, open_source, parse_elo

LICHESS_DB = "https://database.lichess.org/standard/lichess_db_standard_rated_{month}.pgn.zst"
BATCH_GAMES = 500
SHARD_POSITIONS = 1_000_000
VAL_EVERY = 100  # every 100th game goes to the validation set


def _parse_batch(batch: list[tuple[int, str, int, int]]) -> np.ndarray:
    parts = []
    for game_id, text, elo_white, elo_black in batch:
        try:
            positions = game_positions(text, game_id, elo_white, elo_black)
        except Exception:  # a malformed game should never stop a long run
            continue
        if positions is not None and len(positions.records):
            parts.append(positions.records)
    return np.concatenate(parts) if parts else np.zeros(0, dtype=RECORD)


class ShardWriter:
    def __init__(self, directory: Path, prefix: str):
        self.directory, self.prefix = directory, prefix
        self.pending: list[np.ndarray] = []
        self.pending_count = 0
        self.shards = 0
        self.total = 0

    def add(self, records: np.ndarray) -> None:
        if not len(records):
            return
        self.pending.append(records)
        self.pending_count += len(records)
        self.total += len(records)
        if self.pending_count >= SHARD_POSITIONS:
            self.flush()

    def flush(self) -> None:
        if not self.pending:
            return
        np.save(self.directory / f"{self.prefix}_{self.shards:05d}.npy", np.concatenate(self.pending))
        self.shards += 1
        self.pending, self.pending_count = [], 0


class GameFilter:
    """Decides from the headers alone whether a game is worth parsing."""

    def __init__(self, args):
        self.speeds = set(args.speeds)
        self.min_elo, self.max_elo = args.min_elo, args.max_elo
        self.per_bucket = args.per_bucket
        self.bucket_counts: dict[int, int] = {}
        self.rejected: dict[str, int] = {}

    def _reject(self, reason: str) -> None:
        self.rejected[reason] = self.rejected.get(reason, 0) + 1

    def accept(self, game: RawGame) -> tuple[int, int] | None:
        h = game.headers
        if not is_standard(h):
            return self._reject("variant")
        if speed_of(h.get("TimeControl", "")) not in self.speeds:
            return self._reject("speed")
        if h.get("Termination", "Normal") not in ("Normal", "Time forfeit"):
            return self._reject("abandoned")
        white, black = parse_elo(h.get("WhiteElo")), parse_elo(h.get("BlackElo"))
        if white is None or black is None:
            return self._reject("no rating")
        average = (white + black) // 2
        if not (self.min_elo <= average <= self.max_elo):
            return self._reject("rating range")
        if self.per_bucket:
            bucket = average // ELO_STEP
            if self.bucket_counts.get(bucket, 0) >= self.per_bucket:
                return self._reject("rating bucket full")
            self.bucket_counts[bucket] = self.bucket_counts.get(bucket, 0) + 1
        return white, black


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--source", help="PGN file, .pgn.zst archive or URL")
    source.add_argument("--lichess-month", metavar="YYYY-MM", help="stream this month of the Lichess database")
    parser.add_argument("--out", type=Path, required=True, help="output directory")
    parser.add_argument("--max-games", type=int, default=100_000, help="stop after this many games (default 100000)")
    parser.add_argument("--speeds", default="bullet,blitz,rapid,classical",
                        type=lambda s: [x.strip() for x in s.split(",") if x.strip()],
                        help="time controls to keep (default: bullet,blitz,rapid,classical)")
    parser.add_argument("--min-elo", type=int, default=0, help="minimum average rating of the two players")
    parser.add_argument("--max-elo", type=int, default=4000, help="maximum average rating of the two players")
    parser.add_argument("--per-bucket", type=int, default=0,
                        help="at most this many games per 100-point rating band, so strong players are not "
                             "drowned out by the many 1500s (0 = no limit)")
    parser.add_argument("--workers", type=int, default=max(1, mp.cpu_count() - 1), help="parsing processes")
    args = parser.parse_args(argv)

    url = LICHESS_DB.format(month=args.lichess_month) if args.lichess_month else args.source
    args.out.mkdir(parents=True, exist_ok=True)
    train, val = ShardWriter(args.out, "train"), ShardWriter(args.out, "val")
    game_filter = GameFilter(args)

    print(f"Reading {url}", file=sys.stderr)
    started = time.monotonic()
    scanned = kept = 0
    in_flight = []

    def collect(results) -> None:
        for records in results:
            is_val = records["game"] % VAL_EVERY == 0
            train.add(records[~is_val])
            val.add(records[is_val])

    def report() -> None:
        elapsed = time.monotonic() - started
        print(f"  {scanned:>10,} games read  {kept:>9,} kept  {train.total + val.total:>12,} positions  "
              f"{kept / max(elapsed, 1e-9):6.0f} games/s", file=sys.stderr)

    # "spawn" is what Windows always uses; using it everywhere keeps behaviour the same on every OS.
    with mp.get_context("spawn").Pool(args.workers) as pool, open_source(url) as stream:
        batch: list[tuple[int, str, int, int]] = []
        try:
            for game in iter_raw_games(stream):
                scanned += 1
                ratings = game_filter.accept(game)
                if ratings is None:
                    continue
                batch.append((kept, game.text, *ratings))
                kept += 1
                if len(batch) == BATCH_GAMES:
                    in_flight.append(pool.apply_async(_parse_batch, (batch,)))
                    batch = []
                    # Keep the workers busy without holding the whole month in memory.
                    while len(in_flight) > 4 * args.workers:
                        collect([in_flight.pop(0).get()])
                    if kept % (BATCH_GAMES * 40) == 0:
                        report()
                if kept >= args.max_games:
                    break
        except KeyboardInterrupt:
            print("Interrupted: saving what was read so far.", file=sys.stderr)
        if batch:
            in_flight.append(pool.apply_async(_parse_batch, (batch,)))
        collect(r.get() for r in in_flight)

    train.flush()
    val.flush()
    report()
    meta = {
        "source": url,
        "games": kept,
        "games_scanned": scanned,
        "train_positions": train.total,
        "val_positions": val.total,
        "rejected": game_filter.rejected,
        "options": {k: v for k, v in vars(args).items() if k not in ("out", "source", "lichess_month")},
        "record_format": 1,
    }
    (args.out / "meta.json").write_text(json.dumps(meta, indent=2))
    print(f"Saved {train.total:,} training and {val.total:,} validation positions to {args.out} "
          f"in {time.monotonic() - started:.0f} s", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
