"""Ask a trained model what a player would play: python -m chessml.predict --help

    python -m chessml.predict --model runs/medium/best.pt --elo 1500 \\
        --fen "r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3"
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

import chess

from .encoding import SPEEDS
from .predictor import MovePredictor


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--model", type=Path, required=True)
    parser.add_argument("--fen", default=chess.STARTING_FEN)
    parser.add_argument("--elo", type=int, default=1500, help="rating of the player to move")
    parser.add_argument("--opp-elo", type=int, help="opponent's rating (default: the same)")
    parser.add_argument("--speed", choices=SPEEDS, default="blitz")
    parser.add_argument("--clock", type=float, help="fraction of the starting time left, 0-1 (default unknown)")
    parser.add_argument("--top", type=int, default=5)
    args = parser.parse_args(argv)

    board = chess.Board(args.fen)
    predictor = MovePredictor.load(args.model)
    ranked = predictor.predict(board, args.elo, args.opp_elo or args.elo, args.speed, args.clock)
    print(f"A {args.elo} player ({args.speed}) to move in {board.fen()}:")
    for move, probability in ranked[: args.top]:
        print(f"  {board.san(move):8} {100 * probability:5.1f}%")
    return 0


if __name__ == "__main__":
    sys.exit(main())
