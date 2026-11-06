"""Turning chess positions and moves into numbers for the network.

Every position is seen from the side to move: when Black is to move, the board is mirrored so that
the mover's pieces always start at the bottom. One network then serves both colours.

A position is stored compactly (see ``RECORD``) and expanded to input planes on the GPU
(``planes_from_records``), so the training data stays small and training never parses chess.
"""

from __future__ import annotations

import chess
import numpy as np
import torch

# ---------------------------------------------------------------------------
# Moves: 1858 possible moves, in the mover's orientation.
#   * every queen-like move (1..7 squares in 8 directions) and knight jump from every square: 1792
#     (a promotion to a queen is the plain pawn move)
#   * under-promotions to knight, bishop or rook from the 7th to the 8th rank: 66

_QUEEN_DIRECTIONS = [(0, 1), (1, 1), (1, 0), (1, -1), (0, -1), (-1, -1), (-1, 0), (-1, 1)]
_KNIGHT_JUMPS = [(1, 2), (2, 1), (2, -1), (1, -2), (-1, -2), (-2, -1), (-2, 1), (-1, 2)]
_UNDERPROMOTIONS = [chess.KNIGHT, chess.BISHOP, chess.ROOK]


def _build_moves() -> tuple[list[tuple[int, int, int | None]], list[int]]:
    """The moves, and for each the cell of the network's 73 x 64 move planes that scores it.

    Move planes (like Leela Chess Zero's): for every from-square, 56 queen-like moves (8 directions x
    7 distances), 8 knight jumps and 9 under-promotions (3 pieces x 3 directions).
    """
    moves: list[tuple[int, int, int | None]] = []
    planes: list[int] = []
    for square in range(64):
        file, rank = chess.square_file(square), chess.square_rank(square)
        for d, (df, dr) in enumerate(_QUEEN_DIRECTIONS):
            for distance in range(1, 8):
                f, r = file + df * distance, rank + dr * distance
                if not (0 <= f < 8 and 0 <= r < 8):
                    break
                moves.append((square, chess.square(f, r), None))
                planes.append((d * 7 + distance - 1) * 64 + square)
        for j, (df, dr) in enumerate(_KNIGHT_JUMPS):
            f, r = file + df, rank + dr
            if 0 <= f < 8 and 0 <= r < 8:
                moves.append((square, chess.square(f, r), None))
                planes.append((56 + j) * 64 + square)
    for p, piece in enumerate(_UNDERPROMOTIONS):
        for file in range(8):
            for df in (-1, 0, 1):
                if 0 <= file + df < 8:
                    moves.append((chess.square(file, 6), chess.square(file + df, 7), piece))
                    planes.append((64 + p * 3 + df + 1) * 64 + chess.square(file, 6))
    return moves, planes


MOVE_PLANES = 73
MOVES, MOVE_PLANE_INDEX = _build_moves()
NUM_MOVES = len(MOVES)  # 1858
_MOVE_INDEX = {move: i for i, move in enumerate(MOVES)}


def move_index(move: chess.Move, mirrored: bool) -> int:
    """Index of a real-board move. ``mirrored``: the position is seen from Black's side."""
    from_sq, to_sq = move.from_square, move.to_square
    if mirrored:
        from_sq, to_sq = chess.square_mirror(from_sq), chess.square_mirror(to_sq)
    promotion = move.promotion if move.promotion != chess.QUEEN else None
    return _MOVE_INDEX[(from_sq, to_sq, promotion)]


def index_to_move(index: int, board: chess.Board) -> chess.Move:
    """The real-board move for an index, in a position with ``board.turn`` to move."""
    from_sq, to_sq, promotion = MOVES[index]
    if board.turn == chess.BLACK:
        from_sq, to_sq = chess.square_mirror(from_sq), chess.square_mirror(to_sq)
    piece = board.piece_at(from_sq)
    if promotion is None and piece is not None and piece.piece_type == chess.PAWN and chess.square_rank(to_sq) in (0, 7):
        promotion = chess.QUEEN
    return chess.Move(from_sq, to_sq, promotion)


def legal_move_indices(board: chess.Board) -> np.ndarray:
    mirrored = board.turn == chess.BLACK
    return np.fromiter((move_index(m, mirrored) for m in board.legal_moves), dtype=np.int16)


# ---------------------------------------------------------------------------
# Context: who is moving, how strong they are and how much time they have.

SPEEDS = ("bullet", "blitz", "rapid", "classical")
ELO_MIN, ELO_STEP, ELO_BUCKETS = 600, 100, 25  # <700, 700-799, ..., >=3000
CLOCK_BUCKETS = 11  # 0-9: tenths of the starting time left; 10: unknown


def speed_of(time_control: str) -> str | None:
    """Lichess' categories from a PGN TimeControl tag (duration = base + 40 * increment)."""
    if time_control == "-" or "/" in time_control:
        return "correspondence"
    base, _, increment = time_control.partition("+")
    try:
        estimate = int(base) + 40 * int(increment or 0)
    except ValueError:
        return None
    if estimate < 180:
        return "bullet"
    if estimate < 480:
        return "blitz"
    if estimate < 1500:
        return "rapid"
    return "classical"


def speed_id(speed: str | None) -> int:
    """Correspondence and unknown speeds are treated as classical."""
    return SPEEDS.index(speed) if speed in SPEEDS else SPEEDS.index("classical")


def elo_bucket(elo: np.ndarray | torch.Tensor | int):
    if isinstance(elo, torch.Tensor):
        return ((elo.long() - ELO_MIN) // ELO_STEP).clamp(0, ELO_BUCKETS - 1)
    return np.clip((np.asarray(elo, dtype=np.int64) - ELO_MIN) // ELO_STEP, 0, ELO_BUCKETS - 1)


def clock_bucket(seconds_left: float | None, base_seconds: float | None) -> int:
    if seconds_left is None or not base_seconds:
        return CLOCK_BUCKETS - 1
    return int(min(9, max(0, seconds_left / base_seconds * 10)))


# ---------------------------------------------------------------------------
# Compact position records (78 bytes each).

RECORD = np.dtype(
    [
        ("board", np.uint8, 64),  # 0 empty, 1-6 mover's P N B R Q K, 7-12 opponent's
        ("castling", np.uint8),  # bits: mover O-O, mover O-O-O, opponent O-O, opponent O-O-O
        ("ep", np.uint8),  # file of a legal en passant capture, 255 if none
        ("elo_self", np.uint16),
        ("elo_opp", np.uint16),
        ("speed", np.uint8),
        ("clock", np.uint8),
        ("move", np.uint16),  # index into MOVES: what was played
        ("result", np.int8),  # 1 the mover went on to win, 0 draw, -1 loss
        ("ply", np.uint16),
        ("game", np.uint32),
    ]
)


def encode_board(board: chess.Board) -> tuple[np.ndarray, int, int]:
    """(64 piece codes, castling bits, en passant file) from the side to move's point of view."""
    view = board.mirror() if board.turn == chess.BLACK else board
    codes = np.zeros(64, dtype=np.uint8)
    for square, piece in view.piece_map().items():
        codes[square] = piece.piece_type + (0 if piece.color == chess.WHITE else 6)
    castling = (
        int(view.has_kingside_castling_rights(chess.WHITE))
        | int(view.has_queenside_castling_rights(chess.WHITE)) << 1
        | int(view.has_kingside_castling_rights(chess.BLACK)) << 2
        | int(view.has_queenside_castling_rights(chess.BLACK)) << 3
    )
    ep = chess.square_file(view.ep_square) if view.ep_square is not None and view.has_legal_en_passant() else 255
    return codes, castling, ep


NUM_PLANES = 18  # 12 pieces, 4 castling, en passant, a plane of ones (shows the board's edge to padding)


def planes_from_records(records: dict[str, torch.Tensor]) -> torch.Tensor:
    """(B, 18, 8, 8) float input planes from a batch of records already on the target device."""
    board = records["board"].long()  # (B, 64)
    batch = board.shape[0]
    pieces = torch.nn.functional.one_hot(board, 13)[..., 1:]  # (B, 64, 12)
    pieces = pieces.permute(0, 2, 1).reshape(batch, 12, 8, 8).float()

    castling = records["castling"].long()
    bits = torch.stack([(castling >> i) & 1 for i in range(4)], dim=1).float()  # (B, 4)
    castling_planes = bits[:, :, None, None].expand(batch, 4, 8, 8)

    ep = records["ep"].long()
    ep_plane = torch.zeros(batch, 64, device=board.device)
    has_ep = ep < 8
    ep_plane[has_ep, 40 + ep[has_ep]] = 1.0  # the en passant square is on the 6th rank
    ep_plane = ep_plane.reshape(batch, 1, 8, 8)

    ones = torch.ones(batch, 1, 8, 8, device=board.device)
    return torch.cat([pieces, castling_planes, ep_plane, ones], dim=1)


def context_from_records(records: dict[str, torch.Tensor]) -> dict[str, torch.Tensor]:
    return {
        "elo_self": elo_bucket(records["elo_self"]),
        "elo_opp": elo_bucket(records["elo_opp"]),
        "speed": records["speed"].long(),
        "clock": records["clock"].long(),
    }


def records_to_torch(records: np.ndarray, device: torch.device | str) -> dict[str, torch.Tensor]:
    """Moves a numpy array of RECORDs to the device, one tensor per field."""
    out = {}
    for name in RECORD.names:
        array = np.ascontiguousarray(records[name])
        if array.dtype == np.uint16:
            array = array.astype(np.int32)
        elif array.dtype == np.uint32:
            array = array.astype(np.int64)
        out[name] = torch.from_numpy(array).to(device, non_blocking=True)
    return out
