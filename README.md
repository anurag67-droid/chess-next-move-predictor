<div align="center">

# Chess Next Move Predictor

Predicts the move **a specific human player** will make next: not the best move, but *their* move.
It learns from the player's own past games and from a neural network, written from scratch in PyTorch
and trained on millions of Lichess positions, that plays like a human of any rating.

[![Python](https://img.shields.io/badge/Python-3.10+-3776AB?logo=python&logoColor=white)](ml/)
[![PyTorch](https://img.shields.io/badge/PyTorch-CUDA-EE4C2C?logo=pytorch&logoColor=white)](https://pytorch.org)
[![Data](https://img.shields.io/badge/Data-Lichess%20database-black?logo=lichess)](https://database.lichess.org)
[![C++17](https://img.shields.io/badge/App-C++17%20%2B%20SFML-00599C?logo=c%2B%2B)](src/)

<img src="docs/screenshot-player.png" width="860" alt="After 1.e4 c5 2.Nf3 d6 3.d4 cxd4 4.Nxd4 Nf6 5.Nc3 the player card shows that DrNykterstein played 5...a6 in 70% of their games">

</div>

## What it does

Give it a player's username. It downloads their games from Lichess or Chess.com, and for any
position it answers: *what will this player play here, and how likely is each move?*

On each player's newest 10% of games, which it never saw, it predicts the exact move played
**43–49% of the time**, and the move is in its top 3 about **70%** of the time:

| Player | Rating | Positions tested | Opening book | Neural network | Book + network | Top-3 |
|--------|-------:|-----------------:|-------------:|---------------:|---------------:|------:|
| affections (Lichess) | 1661 | 8,954 | 9.8% | 38.8% | **43.0%** | 67.4% |
| Shakiro (Lichess) | 1782 | 11,825 | 10.0% | 45.2% | **48.5%** | 75.2% |
| DrNykterstein (Lichess) | 3186 | 12,732 | 6.5% | 42.2% | **43.3%** | 71.5% |

*Small first model (0.6M parameters, trained on a CPU). A larger model trained on a GPU and per-player fine-tuning are next; see the [roadmap](#roadmap).*

## How it works

```text
 player's games ──► opening book ─────────────────────┐
 (Lichess / Chess.com)  their past choices by position │
                                                       ├──► probability of every legal move
 Lichess database ──► MoveNet (PyTorch) ───────────────┘
 1M games, 66M positions   position + both ratings + time control + clock
```

**1. Per-player opening book.** Every position the player has faced, indexed by its Zobrist hash so
transpositions are found, with what they played and how they scored. It is right about 60% of the
time when it knows the position, but after move 10 it almost never does: every game soon reaches a
position the player has not seen.

**2. MoveNet, a human move model.** A residual CNN that reads the board and predicts a probability
for each of 1858 possible moves. Unlike an engine, it is trained to imitate people, not to win, and
it is told who is playing: the mover's rating, the opponent's rating, the time control and the clock
are embedded and modulate every residual block (FiLM). So it can answer "what does a 1200 play in
blitz with 10 seconds left?" differently from "what does a 2500 play in classical?". A second head
predicts win/draw/loss.

**3. Combining them.** The book's counts and the network's probabilities are mixed as a Bayesian
prior: `p(move) = (times played + α · p_network) / (games in position + α)`. Where the player has a
habit, the book wins; everywhere else, the network fills the gap.

**4. Honest evaluation.** Everything is learned from a player's older games and tested on their
newest ones, so no game ever helps predict itself or an earlier game, and none of the test players'
games are in the network's training data.

Details, training commands and design notes: [ml/README.md](ml/README.md).

## Quick start

```bash
cd ml
python -m venv .venv && source .venv/bin/activate     # Windows: py -m venv .venv; .venv\Scripts\activate
pip install torch --index-url https://download.pytorch.org/whl/cu128   # or /whl/cpu
pip install -r requirements.txt

python ../tools/fetch_games.py lichess DrNykterstein --max 3000
python -m chessml.evaluate ../players/drnykterstein-lichess.pgn --model models/cpu-small.pt
python -m chessml.predict --model models/cpu-small.pt --elo 1500
```

`tools/fetch_games.py` needs only Python and saves games to `players/<user>-<site>.pgn`:

| Option | Meaning |
|--------|---------|
| `--max N` | Keep the newest N games |
| `--speed LIST` | Only `bullet`, `blitz`, `rapid`, `classical`, `correspondence` games |
| `--since` / `--until DATE` | Date range, `YYYY-MM-DD` |
| `--rated-only` | Skip casual games (Lichess) |
| `--token TOKEN` | A Lichess API token, for faster downloads (or set `LICHESS_TOKEN`) |

## Desktop app

A C++ board for exploring a player's repertoire. Load their games in the **Player** card, and for
every position it lists the moves they chose there, how often and how they scored, with an orange
arrow for the most likely one. Click a predicted move to play it, for example to walk through an
opponent's repertoire before a game. It works for both colours and through transpositions, and
filters by bullet, blitz, rapid or classical.

<p align="center">
  <img src="docs/screenshot-game.png" width="860" alt="A Ruy Lopez: the loaded player's orange arrow and the engine's blue arrow both point to Bxc6">
</p>

For contrast, a blue arrow shows the objectively best move, from the search engine of my separate
[chess-engine](https://github.com/anurag67-droid/chess-engine) project. When the two arrows
disagree, the player's habit and the best move part ways.

Building needs a C++17 compiler and CMake 3.16+ (SFML 3, Dear ImGui and chess.hpp are submodules):

```bash
git clone --recursive https://github.com/anurag67-droid/chess-next-move-predictor.git
cd chess-next-move-predictor
cmake -S . -B build
cmake --build build -j
./build/ChessNextMovePredictor
```

On Linux, building the bundled SFML needs
`libx11-dev libxrandr-dev libxcursor-dev libxi-dev libudev-dev libgl1-mesa-dev libfreetype-dev libharfbuzz-dev`.

The build also produces `player-book`, a command-line tool that prints a player's repertoire and
measures the book alone on their newest games (`./build/player-book players/drnykterstein-lichess.pgn`),
and a test suite (`ctest --test-dir build`).

## Roadmap

1. **Per-player opening book** (done).
2. **General human move model** (done): MoveNet, conditioned on rating, time control and clock. A
   medium model (3.4M parameters, 1M games) is training on an RTX 4060.
3. **Personal model**: fine-tune MoveNet on one player's games (`chessml.personal`) and check that it
   beats the general model on the same held-out games.
4. **In the app**: export the model to ONNX and draw its prediction for every position, not only
   the ones in the player's book.

## Project layout

```text
ml/chessml/   the move-prediction pipeline (Python, PyTorch)
              prepare.py   Lichess database → 78-byte position records, on every CPU core
              model.py     MoveNet: residual CNN with FiLM conditioning, policy and win/draw/loss heads
              train.py     mixed-precision GPU training, checkpoints, validation, rating ablation
              personal.py  one player's older games → a fine-tuning set
              evaluate.py  book vs network vs both on a player's newest games
              book.py      the per-player opening book
tools/        fetch_games.py: downloads a player's games from Lichess or Chess.com
src/, include/
├── predictor/  player_book.*   the opening book in C++ (same results as book.py), speed filter, evaluation
├── cli/        player-book     command-line repertoire and accuracy report
├── gui/        the desktop app: board, player card, arrows (SFML + Dear ImGui)
├── core/       game state, SAN history, FEN, draw rules
└── engine/     best-move search for the blue arrow (from chess-engine)
tests/        C++ book tests; ml/tests/ has the Python tests
```

## Limitations

- The network does not yet use the ratings much: setting everyone to 1500 costs the small model
  only 0.1 points of accuracy. Tracking this in the training log is how the bigger model is judged.
- It sees only the current position, not the moves that led to it, so it cannot pick up a
  player's plans across moves.
- The app shows the book's prediction; the network's prediction is not in the app yet (step 4).
