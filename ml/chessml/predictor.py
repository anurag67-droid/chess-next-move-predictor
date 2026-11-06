"""Running a trained MoveNet on positions."""

from __future__ import annotations

import chess
import numpy as np
import torch

from .encoding import RECORD, clock_bucket, encode_board, index_to_move, legal_move_indices, records_to_torch, speed_id
from .encoding import context_from_records, planes_from_records
from .model import MoveNet, load_model


class MovePredictor:
    def __init__(self, model: MoveNet, device: torch.device | str = "cpu"):
        self.model = model.to(device).eval()
        self.device = device

    @classmethod
    def load(cls, path, device: str = "cpu") -> "MovePredictor":
        model, _ = load_model(path, device)
        return cls(model, device)

    @torch.no_grad()
    def probabilities(self, records: np.ndarray, legal: list[np.ndarray], batch_size: int = 2048) -> list[np.ndarray]:
        """For each position, the probability of each of its legal moves (aligned with `legal`)."""
        out: list[np.ndarray] = []
        for start in range(0, len(records), batch_size):
            batch = records_to_torch(records[start : start + batch_size], self.device)
            logits, _ = self.model(planes_from_records(batch), **context_from_records(batch))
            logits = logits.float().cpu().numpy()
            for row, moves in zip(logits, legal[start : start + batch_size]):
                scores = row[moves.astype(np.int64)]
                scores = np.exp(scores - scores.max())
                out.append(scores / scores.sum())
        return out

    def predict(self, board: chess.Board, elo_self: int, elo_opp: int, speed: str = "blitz",
                clock_fraction: float | None = None) -> list[tuple[chess.Move, float]]:
        """The moves a player of this rating would play here, most likely first."""
        codes, castling, ep = encode_board(board)
        record = np.zeros(1, dtype=RECORD)
        record["board"][0] = codes
        record["castling"], record["ep"] = castling, ep
        record["elo_self"], record["elo_opp"] = elo_self, elo_opp
        record["speed"] = speed_id(speed)
        record["clock"] = clock_bucket(clock_fraction, 1.0) if clock_fraction is not None else clock_bucket(None, None)
        legal = legal_move_indices(board)
        probs = self.probabilities(record, [legal])[0]
        ranked = sorted(zip(legal.tolist(), probs.tolist()), key=lambda x: -x[1])
        return [(index_to_move(i, board), p) for i, p in ranked]
