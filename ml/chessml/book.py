"""The player's opening book: what they played before in each position.

A Python version of the C++ PlayerBook used by the app, so the ML side can compare against it and
combine it with the model. tests/test_book.py checks that both give the same results.
"""

from __future__ import annotations

from collections import defaultdict
from dataclasses import dataclass

from .pgn_data import PlayerGames


@dataclass
class BookMove:
    move: int  # index into encoding.MOVES
    games: int
    last_game: int


class PlayerBook:
    def __init__(self, games: PlayerGames, before_game: int | None = None, speeds: set[str] | None = None):
        """Built from the games with index < before_game whose speed is in `speeds` (None = all)."""
        # key -> move -> [games, last game]; each move counts once per game (repetitions).
        self._moves: dict[str, dict[int, list[int]]] = defaultdict(dict)
        end = len(games.games) if before_game is None else before_game
        for game_index, game in enumerate(games.games[:end]):
            if speeds is not None and game.speed not in speeds:
                continue
            seen: set[tuple[str, int]] = set()
            positions = game.positions
            for key, move in zip(positions.keys, positions.records["move"].tolist()):
                if (key, move) in seen:
                    continue
                seen.add((key, move))
                stat = self._moves[key].setdefault(move, [0, -1])
                stat[0] += 1
                stat[1] = game_index

    def lookup(self, key: str) -> list[BookMove]:
        """Most frequent first, ties broken by the most recent game."""
        moves = [BookMove(move, games, last) for move, (games, last) in self._moves.get(key, {}).items()]
        moves.sort(key=lambda m: (-m.games, -m.last_game))
        return moves

    def __len__(self) -> int:
        return len(self._moves)
