"""Reading PGN games (from a file, a .zst archive or a URL) and turning them into position records."""

from __future__ import annotations

import http.client
import io
import re
import sys
import time
import urllib.request
from dataclasses import dataclass, field
from pathlib import Path
from typing import BinaryIO, Iterable, Iterator

import chess
import chess.pgn
import numpy as np

from .encoding import RECORD, clock_bucket, encode_board, legal_move_indices, move_index, speed_id, speed_of

USER_AGENT = "chess-next-move-predictor (+https://github.com/anurag67-droid/chess-next-move-predictor)"
HEADER_RE = re.compile(r'^\[(\w+)\s+"(.*)"\]\s*$')


# ---------------------------------------------------------------------------
# Raw games


class ResumingDownload(io.RawIOBase):
    """Reads a URL to the end, reconnecting where it stopped when the connection drops.

    A dropped connection otherwise looks like a normal end of file, which would silently cut a
    month of games short. The server must support byte ranges (database.lichess.org does).
    """

    def __init__(self, url: str, retries: int = 20):
        self.url, self.retries = url, retries
        self.position = 0
        self.length: int | None = None
        self.response = self._open()

    def _open(self):
        headers = {"User-Agent": USER_AGENT}
        if self.position:
            headers["Range"] = f"bytes={self.position}-"
        response = urllib.request.urlopen(urllib.request.Request(self.url, headers=headers), timeout=60)
        if self.position and response.status != 206:
            raise IOError(f"{self.url} does not support resuming downloads")
        if self.length is None and response.headers.get("Content-Length"):
            self.length = int(response.headers["Content-Length"])
        return response

    def readable(self) -> bool:
        return True

    def readinto(self, buffer) -> int:
        for attempt in range(self.retries + 1):
            try:
                n = self.response.readinto(buffer)
            except (OSError, http.client.HTTPException):
                n = 0
            if n or not buffer or self.length is None or self.position >= self.length:
                self.position += n
                return n
            # Fewer bytes than announced: the connection dropped. Reconnect from where we are.
            print(f"  connection lost at {self.position / 1e9:.2f} GB, reconnecting ...", file=sys.stderr)
            time.sleep(min(60, 2 ** attempt))
            try:
                self.response.close()
                self.response = self._open()
            except (OSError, http.client.HTTPException):
                continue
        raise IOError(f"Gave up downloading {self.url} after {self.retries} reconnections")

    def close(self) -> None:
        self.response.close()
        super().close()


def open_source(source: str | Path) -> io.TextIOBase:
    """A text stream of PGN from a local path or an http(s) URL; .zst archives are decompressed on the fly."""
    source = str(source)
    raw: BinaryIO
    if source.startswith(("http://", "https://")):
        raw = io.BufferedReader(ResumingDownload(source), buffer_size=1 << 20)
    else:
        raw = open(source, "rb")
    if source.endswith(".zst"):
        import zstandard

        raw = zstandard.ZstdDecompressor().stream_reader(raw, read_size=1 << 20)
    return io.TextIOWrapper(raw, encoding="utf-8", errors="replace", newline="")


@dataclass
class RawGame:
    headers: dict[str, str]
    text: str  # the whole game, headers included


def iter_raw_games(lines: Iterable[str]) -> Iterator[RawGame]:
    """Splits PGN text into games without parsing the moves (that is the slow part)."""
    text: list[str] = []
    headers: dict[str, str] = {}
    in_moves = False
    for line in lines:
        stripped = line.strip()
        if stripped.startswith("["):
            if in_moves:
                yield RawGame(headers, "".join(text))
                text, headers, in_moves = [], {}, False
            match = HEADER_RE.match(stripped)
            if match:
                headers[match.group(1)] = match.group(2)
        elif stripped:
            in_moves = True
        text.append(line)
    if headers:
        yield RawGame(headers, "".join(text))


def is_standard(headers: dict[str, str]) -> bool:
    return headers.get("Variant", "Standard").lower() in ("standard", "from position")


def parse_elo(value: str | None) -> int | None:
    try:
        return int(value) if value else None
    except ValueError:
        return None


def base_seconds(time_control: str) -> int | None:
    try:
        return int(time_control.partition("+")[0])
    except ValueError:
        return None


def game_sort_key(headers: dict[str, str]) -> tuple[str, str]:
    return headers.get("UTCDate") or headers.get("Date", ""), headers.get("UTCTime", "")


# ---------------------------------------------------------------------------
# Games -> records


@dataclass
class GamePositions:
    records: np.ndarray  # RECORDs
    keys: list[str] = field(default_factory=list)  # position keys (for the opening book)
    legal: list[np.ndarray] = field(default_factory=list)  # legal move indices per position


def position_key(board: chess.Board) -> str:
    """Identifies a position for the book: pieces, side to move, castling and a *legal* en passant."""
    return board.epd(en_passant="legal")


def game_positions(
    game_text: str,
    game_id: int,
    elo_white: int,
    elo_black: int,
    colors: tuple[bool, ...] = (chess.WHITE, chess.BLACK),
    with_details: bool = False,
) -> GamePositions | None:
    """Records for every position in a game where one of `colors` was to move.

    with_details also returns each position's book key and legal moves (for evaluation).
    Returns None when the game cannot be read.
    """
    game = chess.pgn.read_game(io.StringIO(game_text))
    if game is None:
        return None
    headers = game.headers
    speed = speed_id(speed_of(headers.get("TimeControl", "")))
    base = base_seconds(headers.get("TimeControl", ""))
    result_white = {"1-0": 1, "0-1": -1, "1/2-1/2": 0}.get(headers.get("Result", "*"), 0)

    board = game.board()
    rows = []
    keys: list[str] = []
    legal: list[np.ndarray] = []
    clocks = {chess.WHITE: float(base) if base else None, chess.BLACK: float(base) if base else None}
    ply = 0
    for node in game.mainline():
        move = node.move
        mover = board.turn
        if mover in colors:
            codes, castling, ep = encode_board(board)
            rows.append(
                (
                    codes,
                    castling,
                    ep,
                    elo_white if mover == chess.WHITE else elo_black,
                    elo_black if mover == chess.WHITE else elo_white,
                    speed,
                    clock_bucket(clocks[mover], base),
                    move_index(move, mover == chess.BLACK),
                    result_white if mover == chess.WHITE else -result_white,
                    min(ply, 65535),
                    game_id,
                )
            )
            if with_details:
                keys.append(position_key(board))
                legal.append(legal_move_indices(board))
        clock = node.clock()
        if clock is not None:
            clocks[mover] = clock
        board.push(move)
        ply += 1
    # python-chess stops at an illegal or unreadable move: the positions before it are kept.
    records = np.array(rows, dtype=RECORD) if rows else np.zeros(0, dtype=RECORD)
    return GamePositions(records, keys, legal)


# ---------------------------------------------------------------------------
# One player's games (for evaluation and fine-tuning)


@dataclass
class PlayerGame:
    headers: dict[str, str]
    color: bool  # chess.WHITE or chess.BLACK
    speed: str | None
    positions: GamePositions


@dataclass
class PlayerGames:
    player: str
    games: list[PlayerGame]  # oldest first
    skipped_variant: int = 0
    skipped_not_player: int = 0


def most_common_name(raw_games: list[RawGame]) -> str:
    counts: dict[str, int] = {}
    for g in raw_games:
        for key in ("White", "Black"):
            name = g.headers.get(key, "")
            if name and name != "?":
                counts[name.lower()] = counts.get(name.lower(), 0) + 1
    if not counts:
        raise ValueError("The PGN has no player names")
    # Alphabetical first among equals, like the C++ PlayerBook.
    return max(sorted(counts), key=lambda name: counts[name])


def load_player_games(path: str | Path, player: str | None = None, default_elo: int = 1500) -> PlayerGames:
    """Every game of one player, oldest first, with the positions where the player was to move.

    Follows the same rules as the C++ PlayerBook (src/predictor/player_book.cpp), so both give the same book.
    """
    with open_source(path) as stream:
        raw_games = list(iter_raw_games(stream))
    if not raw_games:
        raise ValueError(f"No games found in {path}")
    wanted = player.lower() if player else most_common_name(raw_games)

    order = sorted(range(len(raw_games)), key=lambda i: game_sort_key(raw_games[i].headers))  # stable
    result = PlayerGames(player=wanted, games=[])
    for index in order:
        raw = raw_games[index]
        if not is_standard(raw.headers):
            result.skipped_variant += 1
            continue
        is_white = raw.headers.get("White", "").lower() == wanted
        is_black = raw.headers.get("Black", "").lower() == wanted
        if is_white == is_black:
            result.skipped_not_player += 1
            continue
        color = chess.WHITE if is_white else chess.BLACK
        elo_white = parse_elo(raw.headers.get("WhiteElo")) or default_elo
        elo_black = parse_elo(raw.headers.get("BlackElo")) or default_elo
        positions = game_positions(raw.text, len(result.games), elo_white, elo_black, (color,), with_details=True)
        if positions is None:
            continue
        result.player = raw.headers["White" if is_white else "Black"]
        result.games.append(PlayerGame(raw.headers, color, speed_of(raw.headers.get("TimeControl", "")), positions))
    if not result.games:
        raise ValueError(f"None of the games in {path} were played by {wanted}")
    return result
