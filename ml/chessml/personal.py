"""Builds a fine-tuning set from one player's games: python -m chessml.personal --help

    python -m chessml.personal ../players/drnykterstein-lichess.pgn --out data/personal-drnykterstein
    python -m chessml.train --data data/personal-drnykterstein --out runs/personal-drnykterstein \\
        --init runs/medium/best.pt --steps 1500 --lr 1e-4 --warmup 100 --eval-every 100

Only the positions where the player was to move, and only from the games chessml.evaluate learns
from: the newest --test-fraction of the games (the ones evaluate tests on) are never written. Of
the rest, the newest --val-fraction become the validation set, so train.py keeps the checkpoint
that best predicts games the player played after the ones it trained on.

    <out>/train_00000.npy, <out>/val_00000.npy, <out>/meta.json   (the same layout as chessml.prepare)
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

import numpy as np

from .encoding import RECORD
from .evaluate import split
from .pgn_data import load_player_games


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("pgn", type=Path, help="the player's games (e.g. from tools/fetch_games.py)")
    parser.add_argument("--out", type=Path, required=True, help="output directory")
    parser.add_argument("--player", help="whose moves to learn (default: the name in the most games)")
    parser.add_argument("--test-fraction", type=float, default=0.1,
                        help="newest games left out for chessml.evaluate (use the same value there)")
    parser.add_argument("--val-fraction", type=float, default=0.1, help="share of the remaining games for validation")
    parser.add_argument("--speed", type=lambda s: {x.strip() for x in s.split(",") if x.strip()},
                        help="only these speeds, e.g. blitz,rapid (use the same value in chessml.evaluate)")
    args = parser.parse_args(argv)

    games = load_player_games(args.pgn, args.player)
    _, cutoff, _ = split(games, args.test_fraction, args.speed)
    learn = [g for g in games.games[:cutoff] if args.speed is None or g.speed in args.speed]
    val_count = int(len(learn) * args.val_fraction)
    if val_count == 0 or val_count == len(learn):
        raise SystemExit(f"Only {len(learn)} games to learn from: not enough to split")

    def records(part) -> np.ndarray:
        parts = [g.positions.records for g in part if len(g.positions.records)]
        return np.concatenate(parts) if parts else np.zeros(0, dtype=RECORD)

    train, val = records(learn[:-val_count]), records(learn[-val_count:])
    args.out.mkdir(parents=True, exist_ok=True)
    np.save(args.out / "train_00000.npy", train)
    np.save(args.out / "val_00000.npy", val)
    meta = {
        "source": str(args.pgn),
        "player": games.player,
        "train_games": len(learn) - val_count,
        "val_games": val_count,
        "test_games_left_out": len(games.games) - cutoff,
        "train_positions": len(train),
        "val_positions": len(val),
        "options": {k: sorted(v) if isinstance(v, set) else v for k, v in vars(args).items() if k not in ("pgn", "out")},
        "record_format": 1,
    }
    (args.out / "meta.json").write_text(json.dumps(meta, indent=2))
    print(f"{games.player}: {len(train):,} training positions ({meta['train_games']} games), {len(val):,} validation "
          f"({val_count} games); the newest {meta['test_games_left_out']} games are left out for testing",
          file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
