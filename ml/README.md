# chessml: our own human move predictor

A neural network, written from scratch in PyTorch, that predicts **the move a human would play**:
not the best move. It takes the player's rating, the opponent's rating, the time control and the
clock into account, so it can play like a 1200 or like a 2400.

| File | What it does |
|------|--------------|
| `chessml/encoding.py` | Board → 18 input planes (seen from the side to move), the 1858-move vocabulary, compact 78-byte position records |
| `chessml/pgn_data.py` | Reads PGN files, `.zst` archives or URLs; one player's games for evaluation |
| `chessml/prepare.py` | Builds a training set from the Lichess database, using every CPU core |
| `chessml/model.py` | MoveNet: a residual CNN, with the ratings, time control and clock fed to every block (FiLM); move and win/draw/loss heads |
| `chessml/train.py` | Training on a GPU (mixed precision) or CPU, with checkpoints, validation and `--resume` |
| `chessml/personal.py` | One player's older games → a fine-tuning set (their moves only, never the test games) |
| `chessml/evaluate.py` | Book vs model vs both on a player's newest games |
| `chessml/predict.py` | Ask the model about any position |
| `chessml/export.py` | A trained model → the `.movenet` file the C++ app runs (batch norm folded in) |
| `chessml/book.py` | The player's opening book (same results as the C++ one in the app) |

## Setup

Python 3.10 to 3.13. On Windows, use PowerShell from the repository folder:

```powershell
cd ml
py -m venv .venv
.venv\Scripts\activate
pip install torch --index-url https://download.pytorch.org/whl/cu128
pip install -r requirements.txt
python -m pytest
```

On Linux or macOS, activate with `source .venv/bin/activate`. Without an NVIDIA GPU, install the CPU build instead:
`pip install torch --index-url https://download.pytorch.org/whl/cpu`.

Check that PyTorch sees the GPU:

```powershell
python -c "import torch; print(torch.cuda.is_available(), torch.cuda.get_device_name(0))"
```

## 1. Build a training set

Every game ever played on Lichess is free to download (database.lichess.org). `prepare` streams one
month straight from the site, so the 30 GB file is never saved. It keeps the games that pass the
filters and stops after `--max-games`:

```powershell
python -m chessml.prepare --lichess-month 2026-10 --out data/lichess-1m --max-games 1000000 --per-bucket 60000
```

- `--per-bucket 60000` keeps at most 60,000 games per 100-point rating band. Most Lichess games are
  between 1400 and 2000; without this the model would rarely see strong players.
- One game gives about 66 positions, and each position takes 78 bytes: 1M games is about 66M positions and 5 GB.
- Speed depends on the CPU, roughly 150–250 games per second per core. Add `--workers N` to choose the number of processes.
- Ctrl+C stops early and keeps what was read.

## 2. Train

```powershell
python -m chessml.train --data data/lichess-1m --out runs/medium --size medium --steps 150000
```

| `--size` | Network | Parameters | Use it for |
|----------|---------|-----------:|------------|
| `small` | 6 blocks × 64 channels | 0.6M | CPU |
| `medium` | 10 blocks × 128 channels | 3.4M | an RTX 4060 overnight |
| `large` | 15 blocks × 192 channels | 11M | more data, more time |

The log prints the training loss and speed every 100 steps, and the validation top-1/top-3 every
`--eval-every` steps, plus how many points of top-1 the ratings add (the same positions with
everyone rated 1500). If that stays near zero, the model is ignoring who is playing. `runs/medium/best.pt` is the best model so far. Stop at any time with Ctrl+C and
continue with the same command plus `--resume`. The data stays on disk (memory-mapped), so a
dataset larger than your RAM is fine; an SSD helps.

On an RTX 4060, `medium` at batch size 1024 should train several thousand positions per second, so
150,000 steps (150M positions, a bit over two passes through 1M games) takes a few hours. If you
run out of GPU memory, use `--batch-size 512`.

## 3. Test it on a real player

```powershell
python -m chessml.evaluate ..\players\drnykterstein-lichess.pgn --model runs\medium\best.pt
```

This learns from the player's older games and tests on their newest 10%. It prints top-1 and top-3
for the book alone, the model alone, and both combined.

```powershell
python -m chessml.predict --model runs\medium\best.pt --elo 1500 --fen "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1"
```

## 4. Use it in the app

```powershell
python -m chessml.export runs\medium\best.pt --out models\medium.movenet
```

The desktop app loads `ml/models/medium.movenet` at start and runs the network in C++
(`src/predictor/move_net.cpp`): no Python, PyTorch or ONNX runtime needed. `--reference FILE` also
writes PyTorch's predictions for a few test positions; `move_net_test <model> <reference>` checks that
the C++ network gives the same ones (it does, to within 10⁻⁶).

## Results so far

Both trained models are included, so you can try them before training your own: `models/medium.pt`
(13.6 MB, the better one) and `models/cpu-small.pt` (2.4 MB). Each has its training log next to it.

```powershell
python -m chessml.predict --model models/medium.pt --elo 1500
```

A first `small` model trained on this cloud session's CPU: 241,720 Lichess games from October 2026
(16M positions, at most 20,000 games per rating band, average rating 1000+), 18,000 steps of 512
(9.2M positions seen, 1¾ hours). Validation games: **44.9% top-1, 72.7% top-3**.

Tested on four players' newest 10% of games (the book uses only their older games; none of these
games are in the training data):

| Player | Rating | Positions | Book | Model | Book + model | Top-3, book + model |
|--------|-------:|----------:|-----:|------:|-------------:|--------------------:|
| affections (Lichess) | 1661 | 8,584 | 10.0% | 38.7% | **43.0%** | 67.7% |
| Shakiro (Lichess) | 1782 | 11,799 | 10.0% | 45.2% | **48.5%** | 75.2% |
| DrNykterstein (Lichess) | 3186 | 12,732 | 6.5% | 42.2% | **43.3%** | 71.4% |
| Hikaru (Chess.com, 300 games) | 3469 | 1,367 | 7.2% | 35.6% | **39.2%** | 68.3% |

The book is right where the player has a habit, but after move 10 it has almost never seen the
position. The model fills that gap: from 0% to 35–45% for moves 11 onwards.

What this small model has *not* learned yet is to use the ratings: with everyone rated 1500, its
accuracy drops by only 0.1 points, and it gives a mate-in-one blunder about the same chance at 800
as at 2400.

### Medium model on an RTX 4060 laptop

`medium` (3.4M parameters) on 1M games from October 2026 (`--per-bucket 60000`), 150,000 steps of
1024 (154M positions, 3h 26m at about 12,000 positions per second); `models/medium.pt` is its best
checkpoint (step 138,000). Validation games: **51.0% top-1,
78.9% top-3**, and the ratings now add **0.7 points** (up from 0.1).

| Player | Model, small → medium | Book + model, small → medium | Top-3, book + model |
|--------|----------------------:|-----------------------------:|--------------------:|
| affections | 38.8% → 42.0% | 43.0% → **45.8%** | 67.4% → 70.7% |
| Shakiro | 45.2% → 51.1% | 48.5% → **54.0%** | 75.2% → 81.2% |
| DrNykterstein | 42.2% → 52.4% | 43.3% → **52.9%** | 71.5% → 82.0% |

Most of the gain is after the opening, where the book has nothing: DrNykterstein's moves 11+ go
from 40% to 51.5%.

### A personal model

```powershell
python -m chessml.personal ..\players\drnykterstein-lichess.pgn --out data\personal-drnykterstein
python -m chessml.train --data data\personal-drnykterstein --out runs\personal-drnykterstein `
    --init runs\medium\best.pt --steps 1500 --lr 1e-4 --warmup 100 --eval-every 100
python -m chessml.evaluate ..\players\drnykterstein-lichess.pgn --model runs\personal-drnykterstein\best.pt
```

For each player: their 2,430 older games to train (73k–105k of their moves), the next 270 to choose
the checkpoint, and the same newest 300 games as above to test. 1,500 steps at learning rate 1e-4,
about two minutes each.

| Newest 300 games, top-1 with the book | Moves 1–10 | 11–25 | 26+ | All | Top-3 | Probability of the move played |
|---------------------------------------|-----------:|------:|----:|----:|------:|-------------------------------:|
| affections, general model | 57.4% | 41.1% | 38.3% | 45.8% | 70.7% | 0.155 |
| affections, personal model | **60.9%** | **44.1%** | **41.0%** | **48.9%** | **73.7%** | **0.179** |
| Shakiro, general model | 66.0% | 49.9% | 49.9% | 54.0% | 81.2% | 0.244 |
| Shakiro, personal model | **70.1%** | **52.0%** | **51.7%** | **56.4%** | **83.7%** | **0.267** |
| DrNykterstein, general model | 57.4% | 51.5% | 51.6% | 52.9% | 82.0% | 0.240 |
| DrNykterstein, personal model | **58.6%** | **52.2%** | **52.7%** | **53.9%** | **82.8%** | **0.249** |

Better for every player and in every phase of the game, not only the opening: the network learns
the player's style, not just their repertoire. The gain is largest for the club players (+3.1 and
+2.4 points) and smallest for the 3186 (+1.0).
The book now adds almost nothing on top (DrNykterstein: 53.8% → 53.9%), because the personal model
has learned the player's openings itself.

## How the model sees a position

- **Board:** 12 planes of 8×8, one per piece type and colour, always from the side to move: when
  Black is to move the board is mirrored, so one network learns both colours. Then 4 castling-right
  planes, the en passant square, and a plane of ones that lets the convolutions see the board's edge.
- **Context:** the mover's rating, the opponent's rating (100-point bands from below 700 to 3000+),
  bullet/blitz/rapid/classical, and the share of the starting time left on the mover's clock. Each
  is a learned embedding; together they scale and shift every residual block's channels (FiLM).
- **Output:** a score for each of 1858 moves: every queen-like move and knight jump from every
  square, plus under-promotions. Illegal moves are masked out when predicting. A second head
  predicts win/draw/loss, which helps the network learn useful features.
