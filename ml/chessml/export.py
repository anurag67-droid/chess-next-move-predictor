"""Exports a trained MoveNet for the C++ app: python -m chessml.export --help

    python -m chessml.export models/medium.pt --out models/medium.movenet

The app (src/predictor/move_net.cpp) reads this file and runs the network itself, so it needs no
PyTorch or ONNX runtime. Batch norm is folded into the convolutions before writing, and the value
head is left out: the app only needs the move probabilities.

File layout, little-endian:
    b"MOVENET1"
    9 x int32    channels, blocks, context_dim, ELO_BUCKETS, len(SPEEDS), CLOCK_BUCKETS,
                 NUM_PLANES, MOVE_PLANES, NUM_MOVES
    4 x NUM_MOVES x int32    each move's from-square, to-square, under-promotion piece
                             (0 none, 2 knight, 3 bishop, 4 rook) and cell in the 73 x 64 move planes
    float32 tensors in PyTorch's layout (out, in, ky, kx), in this order:
        elo_self, elo_opp, speed, clock embeddings; context linear weight, bias
        stem conv weight, bias
        per block: conv1 weight, bias; conv2 weight, bias; FiLM weight, bias
        policy conv weight, bias; policy 1x1 conv weight, bias

--reference FILE also writes the network's predictions for a few test positions, which
tests/move_net_test.cpp compares with its own.
"""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

import chess
import numpy as np
import torch
import torch.nn as nn

from .encoding import CLOCK_BUCKETS, ELO_BUCKETS, MOVE_PLANE_INDEX, MOVE_PLANES, MOVES, NUM_PLANES, SPEEDS
from .model import MoveNet, load_model
from .predictor import MovePredictor

MAGIC = b"MOVENET1"

# (FEN, mover's rating, opponent's rating, speed, share of the clock left or None)
REFERENCE_POSITIONS = [
    (chess.STARTING_FEN, 1500, 1500, "blitz", None),
    (chess.STARTING_FEN, 800, 2400, "bullet", 0.05),
    ("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1", 2200, 2100, "rapid", 0.7),
    ("r3k2r/pppq1ppp/2npbn2/2b1p3/2B1P3/2NPBN2/PPPQ1PPP/R3K2R w KQkq - 6 8", 1700, 1650, "classical", 0.4),
    ("r3k2r/pppq1ppp/2npbn2/2b1p3/2B1P3/2NPBN2/PPPQ1PPP/R3K2R b Kq - 6 8", 3000, 2900, "blitz", 0.95),
    ("rnbqkbnr/ppp1p1pp/8/3pPp2/8/8/PPPP1PPP/RNBQKBNR w KQkq f6 0 3", 1300, 1350, "blitz", None),
    ("rnbqkb1r/pppp1ppp/5n2/8/3pP3/2P5/PP3PPP/RNBQKBNR b KQkq e3 0 4", 1900, 1900, "rapid", 0.5),
    ("8/1P4k1/8/8/8/8/5Kp1/8 w - - 0 60", 2500, 2500, "bullet", 0.1),
    ("8/1P4k1/8/8/8/8/6p1/4K3 b - - 0 60", 650, 3100, "classical", 0.0),
    ("rnbqkbnr/ppp2ppp/3p4/1B2p3/4P3/8/PPPP1PPP/RNBQK1NR b KQkq - 1 3", 1100, 1200, "blitz", 0.6),
]


def _fold(conv: nn.Conv2d, bn: nn.BatchNorm2d) -> tuple[np.ndarray, np.ndarray]:
    """Weight and bias of one convolution that computes bn(conv(x)) in evaluation mode."""
    scale = bn.weight / torch.sqrt(bn.running_var + bn.eps)
    weight = conv.weight * scale[:, None, None, None]
    bias = bn.bias - bn.running_mean * scale
    if conv.bias is not None:
        bias = bias + conv.bias * scale
    return weight.detach().numpy(), bias.detach().numpy()


def _tensors(model: MoveNet) -> list[np.ndarray]:
    t = lambda p: p.detach().numpy()  # noqa: E731
    out = [t(model.elo_self.weight), t(model.elo_opp.weight), t(model.speed.weight), t(model.clock.weight)]
    out += [t(model.context[1].weight), t(model.context[1].bias)]
    out += _fold(model.stem[0], model.stem[1])
    for block in model.blocks:
        out += _fold(block.conv1, block.bn1)
        out += _fold(block.conv2, block.bn2)
        out += [t(block.film.weight), t(block.film.bias)]
    out += _fold(model.policy[0], model.policy[1])
    out += [t(model.policy[3].weight), t(model.policy[3].bias)]
    return out


def export(model: MoveNet, path: Path) -> None:
    model = model.cpu().eval()
    c = model.config
    header = [c.channels, c.blocks, c.context_dim, ELO_BUCKETS, len(SPEEDS), CLOCK_BUCKETS, NUM_PLANES,
              MOVE_PLANES, len(MOVES)]
    promotion = {None: 0, chess.KNIGHT: 2, chess.BISHOP: 3, chess.ROOK: 4}
    table = np.array([[m[0] for m in MOVES], [m[1] for m in MOVES], [promotion[m[2]] for m in MOVES],
                      MOVE_PLANE_INDEX], dtype="<i4")
    with torch.no_grad(), open(path, "wb") as f:
        f.write(MAGIC)
        f.write(struct.pack("<9i", *header))
        f.write(table.tobytes())
        for tensor in _tensors(model):
            f.write(np.ascontiguousarray(tensor, dtype="<f4").tobytes())


def write_reference(model: MoveNet, path: Path) -> None:
    """One line per position: FEN | elo_self elo_opp speed clock | uci probability ..., most likely first."""
    predictor = MovePredictor(model.cpu().eval())
    with open(path, "w", newline="\n") as f:
        for fen, elo_self, elo_opp, speed, clock in REFERENCE_POSITIONS:
            ranked = predictor.predict(chess.Board(fen), elo_self, elo_opp, speed, clock)
            moves = " ".join(f"{move.uci()} {p:.6f}" for move, p in ranked)
            f.write(f"{fen} | {elo_self} {elo_opp} {speed} {-1 if clock is None else clock} | {moves}\n")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("checkpoint", type=Path, help="a model saved by chessml.train")
    parser.add_argument("--out", type=Path, help="output file (default: the checkpoint's name with .movenet)")
    parser.add_argument("--reference", type=Path, help="also write the network's predictions for test positions")
    args = parser.parse_args(argv)

    model, _ = load_model(args.checkpoint)
    out = args.out or args.checkpoint.with_suffix(".movenet")
    export(model, out)
    print(f"Wrote {out} ({out.stat().st_size / 1e6:.1f} MB)", file=sys.stderr)
    if args.reference:
        write_reference(model, args.reference)
        print(f"Wrote {args.reference}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
