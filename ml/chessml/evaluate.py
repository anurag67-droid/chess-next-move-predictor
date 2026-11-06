"""How well can we predict one player's moves? python -m chessml.evaluate --help

    python -m chessml.evaluate ../players/drnykterstein-lichess.pgn --model runs/medium/best.pt

Splits the player's games by date: everything is learned from the older games and tested on the
newest --test-fraction of them, so nothing is tested on a game it has seen. Three predictors:

  book        what the player chose most often in this exact position before (step 1)
  model       the general model (step 2), given the player's and opponent's ratings
  book+model  the model's probabilities plus the book's counts:
              p(move) = (times played + alpha * model probability) / (games in position + alpha)
              so the book takes over where the player has a habit, and the model fills every gap

Top-1: the predictor's first choice was the move played. Top-3: it was among its first three.
"""

from __future__ import annotations

import argparse
import sys
from dataclasses import dataclass, field
from pathlib import Path

import numpy as np

from .book import PlayerBook
from .pgn_data import PlayerGames, load_player_games
from .predictor import MovePredictor

BUCKETS = (("Moves 1-10", 1, 10), ("Moves 11-25", 11, 25), ("Moves 26+", 26, 10**9))


@dataclass
class Score:
    positions: int = 0
    top1: int = 0
    top3: int = 0
    log_likelihood: float = 0.0  # sum of ln p(move played); only for predictors with probabilities
    in_book: int = 0

    def add(self, ranking: list[int], played: int, probability: float | None = None, in_book: bool = False):
        self.positions += 1
        self.top1 += bool(ranking) and ranking[0] == played
        self.top3 += played in ranking[:3]
        self.in_book += in_book
        if probability is not None:
            self.log_likelihood += float(np.log(max(probability, 1e-9)))


@dataclass
class Report:
    train_games: int = 0
    test_games: int = 0
    scores: dict[str, dict[str, Score]] = field(default_factory=dict)  # predictor -> bucket -> score

    def score(self, predictor: str, bucket: str) -> Score:
        return self.scores.setdefault(predictor, {}).setdefault(bucket, Score())


def split(games: PlayerGames, test_fraction: float, speeds: set[str] | None) -> tuple[list[int], int, int]:
    """(test game indices, index of the first test game, number of training games).

    The test games are the newest test_fraction of the games whose speed is in `speeds`.
    """
    eligible = [i for i, g in enumerate(games.games) if speeds is None or g.speed in speeds]
    test_count = int(len(eligible) * min(max(test_fraction, 0.0), 1.0))
    train_count = len(eligible) - test_count
    if test_count == 0 or train_count == 0:
        return [], len(games.games), train_count
    return eligible[train_count:], eligible[train_count], train_count


def evaluate_player(
    games: PlayerGames,
    predictor: MovePredictor | None,
    test_fraction: float = 0.1,
    speeds: set[str] | None = None,
    alpha: float = 2.0,
) -> Report:
    test, cutoff, train_count = split(games, test_fraction, speeds)
    report = Report(train_games=train_count, test_games=len(test))
    if not test:
        return report
    book = PlayerBook(games, before_game=cutoff, speeds=speeds)

    records = np.concatenate([games.games[i].positions.records for i in test])
    legal = [m for i in test for m in games.games[i].positions.legal]
    keys = [k for i in test for k in games.games[i].positions.keys]
    model_probs = predictor.probabilities(records, legal) if predictor else None

    for n in range(len(records)):
        played = int(records["move"][n])
        move_number = int(records["ply"][n]) // 2 + 1
        buckets = ["All moves"] + [name for name, lo, hi in BUCKETS if lo <= move_number <= hi]
        known = book.lookup(keys[n])
        book_ranking = [m.move for m in known]
        for bucket in buckets:
            report.score("book", bucket).add(book_ranking, played, in_book=bool(known))

        if model_probs is None:
            continue
        moves, probs = legal[n].astype(np.int64), model_probs[n]
        order = np.argsort(-probs)
        played_at = np.flatnonzero(moves == played)
        p_model = float(probs[played_at[0]]) if len(played_at) else 0.0

        counts = np.zeros(len(moves))
        index_of = {int(m): j for j, m in enumerate(moves)}
        for m in known:
            if m.move in index_of:
                counts[index_of[m.move]] = m.games
        combined = (counts + alpha * probs) / (counts.sum() + alpha)
        combined_order = np.argsort(-combined, kind="stable")
        p_combined = float(combined[played_at[0]]) if len(played_at) else 0.0

        for bucket in buckets:
            report.score("model", bucket).add(moves[order].tolist(), played, p_model)
            report.score("book+model", bucket).add(moves[combined_order].tolist(), played, p_combined)
    return report


def print_report(report: Report, out=sys.stdout) -> None:
    print(f"\nBook from the oldest {report.train_games} games, tested on the newest {report.test_games}\n", file=out)
    if not report.scores:
        print("  (not enough games to split)", file=out)
        return
    buckets = [name for name, _, _ in BUCKETS] + ["All moves"]
    print(f"  {'':12} {'positions':>9} {'in book':>8} | {'top-1':>21} | {'top-3':>21}", file=out)
    print(f"  {'':12} {'':>9} {'':>8} | {'book':>6} {'model':>6} {'both':>7} | {'book':>6} {'model':>6} {'both':>7}",
          file=out)
    pct = lambda a, b: f"{100 * a / b:5.1f}%" if b else "    -"
    has_model = "model" in report.scores
    for bucket in buckets:
        b = report.scores["book"].get(bucket)
        if not b or not b.positions:
            continue
        m = report.scores["model"][bucket] if has_model else None
        c = report.scores["book+model"][bucket] if has_model else None
        row = f"  {bucket:12} {b.positions:>9} {pct(b.in_book, b.positions):>8} | {pct(b.top1, b.positions):>6} "
        row += f"{pct(m.top1, m.positions) if m else '':>6} {pct(c.top1, c.positions) if c else '':>7} | "
        row += f"{pct(b.top3, b.positions):>6} {pct(m.top3, m.positions) if m else '':>6} "
        row += f"{pct(c.top3, c.positions) if c else '':>7}"
        print(row, file=out)
    if has_model:
        m, c = report.scores["model"]["All moves"], report.scores["book+model"]["All moves"]
        print(f"\n  Average probability given to the move played (geometric mean): model "
              f"{np.exp(m.log_likelihood / m.positions):.3f}, book+model {np.exp(c.log_likelihood / c.positions):.3f}",
              file=out)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("pgn", type=Path, help="the player's games (e.g. from tools/fetch_games.py)")
    parser.add_argument("--model", type=Path, help="a trained model (.pt); without it only the book is tested")
    parser.add_argument("--player", help="whose moves to predict (default: the name in the most games)")
    parser.add_argument("--test-fraction", type=float, default=0.1)
    parser.add_argument("--speed", type=lambda s: {x.strip() for x in s.split(",") if x.strip()},
                        help="only these speeds, e.g. blitz,rapid")
    parser.add_argument("--alpha", type=float, default=2.0, help="weight of the model against the book's counts")
    parser.add_argument("--device", default="cpu")
    args = parser.parse_args(argv)

    games = load_player_games(args.pgn, args.player)
    elos = [int(g.positions.records["elo_self"][0]) for g in games.games if len(g.positions.records)]
    print(f"Player   {games.player}\nGames    {len(games.games)}  (median rating {int(np.median(elos)) if elos else '?'})")
    predictor = MovePredictor.load(args.model, args.device) if args.model else None
    print_report(evaluate_player(games, predictor, args.test_fraction, args.speed, args.alpha))
    return 0


if __name__ == "__main__":
    sys.exit(main())
