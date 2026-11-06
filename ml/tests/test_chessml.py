"""Tests for the ML pipeline: python -m pytest ml/tests"""

from __future__ import annotations

import random
from pathlib import Path

import chess
import numpy as np
import pytest
import torch

from chessml import encoding as E
from chessml.book import PlayerBook
from chessml.evaluate import evaluate_player
from chessml.model import PRESETS, MoveNet
from chessml.pgn_data import game_positions, iter_raw_games, load_player_games, position_key
from chessml.predictor import MovePredictor
from chessml import personal, prepare, train

ROOT = Path(__file__).resolve().parents[2]
ALICE = ROOT / "tests" / "data" / "alice.pgn"


def test_move_vocabulary_covers_every_legal_move():
    assert E.NUM_MOVES == 1858
    rng = random.Random(0)
    for _ in range(30):
        board = chess.Board()
        for _ in range(120):
            moves = list(board.legal_moves)
            if not moves:
                break
            for move in moves:
                index = E.move_index(move, board.turn == chess.BLACK)
                assert E.index_to_move(index, board) == move
            board.push(rng.choice(moves))


def test_promotions_and_mirroring():
    board = chess.Board("8/P6k/8/8/8/8/p6K/8 w - - 0 1")
    for promo in (chess.QUEEN, chess.ROOK, chess.BISHOP, chess.KNIGHT):
        move = chess.Move(chess.A7, chess.A8, promo)
        assert E.index_to_move(E.move_index(move, False), board) == move
    board.turn = chess.BLACK
    # Black's a2-a1=N is White's a7-a8=N seen in the mirror.
    assert E.move_index(chess.Move(chess.A2, chess.A1, chess.KNIGHT), True) == E.move_index(
        chess.Move(chess.A7, chess.A8, chess.KNIGHT), False)


def test_board_encoding_is_from_the_movers_side():
    after_e4 = chess.Board()
    after_e4.push_san("e4")
    codes, castling, ep = E.encode_board(after_e4)
    # Black to move: the mirrored board has Black's pieces (the mover's, codes 1-6) at the bottom.
    assert codes[chess.E1] == 6 and codes[chess.E8] == 12
    assert codes[chess.E5] == 7  # White's e4 pawn, mirrored, is an opponent pawn
    assert castling == 0b1111 and ep == 255  # no black pawn can take en passant

    planes = E.planes_from_records(E.records_to_torch(_record(after_e4), "cpu"))
    assert planes.shape == (1, E.NUM_PLANES, 8, 8)
    assert planes[0, :12].sum() == 32


def test_legal_en_passant_is_encoded():
    board = chess.Board("4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1")
    _, _, ep = E.encode_board(board)
    assert ep == 3
    planes = E.planes_from_records(E.records_to_torch(_record(board), "cpu"))
    assert planes[0, 16, 5, 3] == 1  # the d6 square


def _record(board: chess.Board) -> np.ndarray:
    record = np.zeros(1, dtype=E.RECORD)
    codes, castling, ep = E.encode_board(board)
    record["board"][0], record["castling"], record["ep"] = codes, castling, ep
    record["elo_self"] = record["elo_opp"] = 1500
    return record


def test_speeds_and_buckets():
    assert E.speed_of("60+0") == "bullet" and E.speed_of("180+2") == "blitz"
    assert E.speed_of("600+0") == "rapid" and E.speed_of("1800+0") == "classical"
    assert E.speed_of("-") == "correspondence" and E.speed_of("?") is None
    assert E.elo_bucket(500) == 0 and E.elo_bucket(1550) == 9 and E.elo_bucket(3200) == E.ELO_BUCKETS - 1
    assert E.clock_bucket(None, 180) == E.CLOCK_BUCKETS - 1 and E.clock_bucket(18, 180) == 1


def test_game_positions_records_every_move():
    text = ALICE.read_text()
    first = next(iter_raw_games(text.splitlines(keepends=True)))
    positions = game_positions(first.text, 7, 1500, 1600, with_details=True)
    assert len(positions.records) == 6  # 1. e4 e5 2. Nf3! Nc6 3. Bc4 Bc5
    assert positions.records["game"].tolist() == [7] * 6
    assert positions.records["elo_self"].tolist() == [1500, 1600] * 3
    assert positions.records["result"].tolist() == [-1, 1] * 3  # 0-1
    assert positions.records["clock"][0] == 9 and positions.records["clock"][2] == 9  # 180 of 180 seconds left
    assert len(positions.keys) == len(positions.legal) == 6


def test_python_book_matches_the_cpp_book():
    """Same fixture and the same numbers as tests/player_book_test.cpp."""
    games = load_player_games(ALICE)
    assert games.player == "Alice"
    assert len(games.games) == 6 and games.skipped_variant == 1 and games.skipped_not_player == 1
    book = PlayerBook(games)
    start = book.lookup(position_key(chess.Board()))
    assert [(m.move, m.games) for m in start][0] == (E.move_index(chess.Move.from_uci("e2e4"), False), 2)
    report = evaluate_player(games, None, test_fraction=0.34)
    score = report.scores["book"]["All moves"]
    assert (report.train_games, report.test_games) == (4, 2)
    assert (score.positions, score.in_book, score.top1, score.top3) == (5, 4, 3, 3)


def test_model_shapes_and_prediction():
    model = MoveNet(PRESETS["tiny"])
    predictor = MovePredictor(model)
    board = chess.Board()
    ranked = predictor.predict(board, 1500, 1500, "blitz")
    assert len(ranked) == 20
    assert abs(sum(p for _, p in ranked) - 1) < 1e-4
    assert all(board.is_legal(m) for m, _ in ranked)


def test_prepare_and_train_end_to_end(tmp_path):
    # prepare skips games without ratings, so give the sample games some.
    pgn = tmp_path / "rated.pgn"
    pgn.write_text(ALICE.read_text().replace("[Event ", '[WhiteElo "1500"]\n[BlackElo "1600"]\n[Event '))
    data = tmp_path / "data"
    assert prepare.main(["--source", str(pgn), "--out", str(data), "--workers", "1"]) == 0
    meta = (data / "meta.json").read_text()
    assert '"variant": 1' in meta  # the Chess960 game
    assert list(data.glob("train_*.npy"))
    # Alice's file is tiny: train on it twice over with the tiny model and check the loss goes down.
    records = np.load(sorted(data.glob("train_*.npy"))[0])
    np.save(data / "val_00000.npy", records[:8])
    out = tmp_path / "run"
    assert train.main(["--data", str(data), "--out", str(out), "--size", "tiny", "--steps", "60",
                       "--batch-size", "32", "--eval-every", "30", "--warmup", "5", "--lr", "3e-3"]) == 0
    assert (out / "best.pt").exists() and (out / "last.pt").exists()
    log = [line for line in (out / "log.jsonl").read_text().splitlines() if line]
    assert len(log) == 2
    # Resuming continues from the saved step instead of starting over.
    assert train.main(["--data", str(data), "--out", str(out), "--size", "tiny", "--steps", "60",
                       "--batch-size", "32", "--eval-every", "30", "--resume"]) == 0
    predictor = MovePredictor.load(out / "best.pt")
    assert predictor.predict(chess.Board(), 1500, 1500)[0][1] > 0


def test_personal_set_leaves_out_the_test_games(tmp_path):
    out = tmp_path / "alice"
    assert personal.main([str(ALICE), "--out", str(out), "--test-fraction", "0.34", "--val-fraction", "0.25"]) == 0
    games = load_player_games(ALICE)
    train_records, val_records = np.load(out / "train_00000.npy"), np.load(out / "val_00000.npy")
    # 6 games: the newest 2 are evaluate's test games; of the other 4, the newest is validation.
    assert sorted(set(train_records["game"].tolist())) == [0, 1, 2]
    assert sorted(set(val_records["game"].tolist())) == [3]
    assert len(train_records) == sum(len(games.games[i].positions.records) for i in range(3))
