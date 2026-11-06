"""Trains MoveNet on prepared positions: python -m chessml.train --help

    python -m chessml.train --data data/lichess --out runs/medium --size medium --steps 200000

Uses a CUDA GPU when there is one (with bfloat16/float16 mixed precision), else the CPU.
Every --eval-every steps it measures top-1 / top-3 move accuracy on the validation games and saves
<out>/last.pt; the best checkpoint so far is <out>/best.pt. Run the same command again with --resume
to continue after a stop.
"""

from __future__ import annotations

import argparse
import json
import math
import sys
import time
from pathlib import Path

import numpy as np
import torch
import torch.nn.functional as F

from .encoding import context_from_records, planes_from_records, records_to_torch
from .model import PRESETS, ModelConfig, MoveNet, count_parameters, load_model, save_checkpoint


class ShardedPositions:
    """All shards of a prepared dataset, memory-mapped: the data stays on disk and only the rows a
    batch needs are read, so a dataset can be much larger than the computer's memory."""

    def __init__(self, directory: Path, prefix: str, limit: int | None = None):
        files = sorted(directory.glob(f"{prefix}_*.npy"))
        if not files:
            raise SystemExit(f"No {prefix}_*.npy files in {directory}; run chessml.prepare first")
        self.shards: list[np.ndarray] = []
        total = 0
        for f in files:
            shard = np.load(f, mmap_mode="r")
            if limit and total + len(shard) > limit:
                shard = shard[: limit - total]
            self.shards.append(shard)
            total += len(shard)
            if limit and total >= limit:
                break
        self.ends = np.cumsum([len(s) for s in self.shards])

    def __len__(self) -> int:
        return int(self.ends[-1])

    def __getitem__(self, indices) -> np.ndarray:
        """Rows by global index (a slice or an array of indices)."""
        if isinstance(indices, slice):
            indices = np.arange(*indices.indices(len(self)))
        indices = np.asarray(indices)
        shard_of = np.searchsorted(self.ends, indices, side="right")
        out = np.empty(len(indices), dtype=self.shards[0].dtype)
        for s in np.unique(shard_of):
            mask = shard_of == s
            local = indices[mask] - (self.ends[s - 1] if s else 0)
            out[mask] = self.shards[s][local]
        return out


def pick_device(name: str) -> torch.device:
    if name != "auto":
        return torch.device(name)
    if torch.cuda.is_available():
        return torch.device("cuda")
    if getattr(torch.backends, "mps", None) and torch.backends.mps.is_available():
        return torch.device("mps")
    return torch.device("cpu")


def forward(model: MoveNet, batch: dict[str, torch.Tensor]):
    return model(planes_from_records(batch), **context_from_records(batch))


def losses(model: MoveNet, batch: dict[str, torch.Tensor], value_weight: float):
    policy, value = forward(model, batch)
    target = batch["move"].long()
    policy_loss = F.cross_entropy(policy.float(), target)
    value_loss = F.cross_entropy(value.float(), batch["result"].long() + 1)  # -1/0/1 -> 0/1/2
    return policy_loss + value_weight * value_loss, policy_loss, value_loss, policy


@torch.no_grad()
def evaluate(model: MoveNet, data: np.ndarray, device, batch_size: int, autocast) -> dict[str, float]:
    model.eval()
    top1 = top3 = 0
    loss_sum = 0.0
    for start in range(0, len(data), batch_size):
        batch = records_to_torch(data[start : start + batch_size], device)
        with autocast():
            policy, _ = forward(model, batch)
        policy = policy.float()
        target = batch["move"].long()
        loss_sum += F.cross_entropy(policy, target, reduction="sum").item()
        best3 = policy.topk(3, dim=1).indices
        top1 += (best3[:, 0] == target).sum().item()
        top3 += (best3 == target[:, None]).any(dim=1).sum().item()
    model.train()
    n = max(1, len(data))
    return {"loss": loss_sum / n, "top1": top1 / n, "top3": top3 / n}


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--data", type=Path, required=True, help="directory written by chessml.prepare")
    parser.add_argument("--out", type=Path, required=True, help="directory for checkpoints and the log")
    parser.add_argument("--size", choices=sorted(PRESETS), default="medium", help="model size (default medium)")
    parser.add_argument("--steps", type=int, default=100_000, help="training steps (default 100000)")
    parser.add_argument("--batch-size", type=int, default=1024)
    parser.add_argument("--lr", type=float, default=2e-3, help="peak learning rate")
    parser.add_argument("--weight-decay", type=float, default=1e-4)
    parser.add_argument("--warmup", type=int, default=1000, help="steps of linear learning-rate warm-up")
    parser.add_argument("--value-weight", type=float, default=0.5, help="weight of the win/draw/loss loss")
    parser.add_argument("--eval-every", type=int, default=2000)
    parser.add_argument("--val-positions", type=int, default=200_000, help="validation positions to use")
    parser.add_argument("--max-positions", type=int, default=0, help="load at most this many training positions")
    parser.add_argument("--device", default="auto", help="auto, cuda, cpu or mps")
    parser.add_argument("--init", type=Path, help="start from this checkpoint's weights (e.g. to fine-tune)")
    parser.add_argument("--resume", action="store_true", help="continue from <out>/last.pt")
    parser.add_argument("--seed", type=int, default=0)
    args = parser.parse_args(argv)

    torch.manual_seed(args.seed)
    rng = np.random.default_rng(args.seed)
    device = pick_device(args.device)
    args.out.mkdir(parents=True, exist_ok=True)

    print(f"Loading {args.data} ...", file=sys.stderr)
    train = ShardedPositions(args.data, "train", args.max_positions or None)
    val = ShardedPositions(args.data, "val", args.val_positions)[: args.val_positions]
    print(f"  {len(train):,} training positions, {len(val):,} validation positions", file=sys.stderr)

    if args.init:
        model, _ = load_model(args.init, device)
        model.train()
    else:
        model = MoveNet(PRESETS[args.size]).to(device)
    optimizer = torch.optim.AdamW(model.parameters(), lr=args.lr, weight_decay=args.weight_decay)

    use_bf16 = device.type == "cuda" and torch.cuda.is_bf16_supported()
    amp_dtype = torch.bfloat16 if use_bf16 else torch.float16
    use_amp = device.type == "cuda"
    scaler = torch.amp.GradScaler("cuda", enabled=use_amp and not use_bf16)

    def autocast():
        return torch.autocast(device.type, dtype=amp_dtype, enabled=use_amp)

    step, best_top1 = 0, -1.0
    last_path, best_path = args.out / "last.pt", args.out / "best.pt"
    if args.resume and last_path.exists():
        checkpoint = torch.load(last_path, map_location=device, weights_only=False)
        model = MoveNet(ModelConfig(**checkpoint["config"])).to(device)
        model.load_state_dict(checkpoint["model"])
        optimizer = torch.optim.AdamW(model.parameters(), lr=args.lr, weight_decay=args.weight_decay)
        optimizer.load_state_dict(checkpoint["optimizer"])
        step, best_top1 = checkpoint["step"], checkpoint.get("best_top1", -1.0)
        print(f"  resumed at step {step}", file=sys.stderr)

    if device.type == "cuda":
        model = model.to(memory_format=torch.channels_last)
        torch.backends.cudnn.benchmark = True

    def learning_rate(s: int) -> float:
        if s < args.warmup:
            return args.lr * (s + 1) / args.warmup
        progress = (s - args.warmup) / max(1, args.steps - args.warmup)
        return args.lr * 0.5 * (1 + math.cos(math.pi * min(1.0, progress)))

    print(f"Training {args.size} model ({count_parameters(model) / 1e6:.1f}M parameters) on {device}"
          f"{' with ' + str(amp_dtype).replace('torch.', '') if use_amp else ''}", file=sys.stderr)
    log = open(args.out / "log.jsonl", "a")
    model.train()
    started, window_loss, window_acc, window_n = time.monotonic(), 0.0, 0, 0
    while step < args.steps:
        for group in optimizer.param_groups:
            group["lr"] = learning_rate(step)
        indices = np.sort(rng.integers(0, len(train), args.batch_size))  # sorted: friendlier memory access
        batch = records_to_torch(train[indices], device)
        with autocast():
            loss, policy_loss, _, policy = losses(model, batch, args.value_weight)
        optimizer.zero_grad(set_to_none=True)
        scaler.scale(loss).backward()
        scaler.unscale_(optimizer)
        torch.nn.utils.clip_grad_norm_(model.parameters(), 5.0)
        scaler.step(optimizer)
        scaler.update()
        step += 1

        window_loss += policy_loss.item()
        window_acc += (policy.argmax(dim=1) == batch["move"].long()).sum().item()
        window_n += 1
        if step % 100 == 0:
            elapsed = time.monotonic() - started
            print(f"  step {step:>7}  loss {window_loss / window_n:.3f}  train top-1 "
                  f"{window_acc / (window_n * args.batch_size):.3f}  lr {learning_rate(step):.2e}  "
                  f"{step * args.batch_size / elapsed:,.0f} pos/s", file=sys.stderr)
            window_loss, window_acc, window_n = 0.0, 0, 0

        if step % args.eval_every == 0 or step == args.steps:
            metrics = evaluate(model, val, device, 4096, autocast)
            # How much do the ratings help? The same positions with everyone rated 1500.
            unrated = val[:50_000].copy()
            unrated["elo_self"] = unrated["elo_opp"] = 1500
            rated_top1 = evaluate(model, val[:50_000], device, 4096, autocast)["top1"]
            metrics["rating_gain"] = rated_top1 - evaluate(model, unrated, device, 4096, autocast)["top1"]
            improved = metrics["top1"] > best_top1
            best_top1 = max(best_top1, metrics["top1"])
            entry = {"step": step, "seconds": round(time.monotonic() - started), **metrics}
            log.write(json.dumps(entry) + "\n")
            log.flush()
            print(f"  [eval] step {step}  val loss {metrics['loss']:.3f}  top-1 {metrics['top1']:.3f}  "
                  f"top-3 {metrics['top3']:.3f}  ratings add {100 * metrics['rating_gain']:+.1f} pts"
                  f"{'  (best)' if improved else ''}", file=sys.stderr)
            extra = {"optimizer": optimizer.state_dict(), "step": step, "best_top1": best_top1,
                     "metrics": metrics, "args": {k: str(v) for k, v in vars(args).items()}}
            save_checkpoint(last_path, model, extra)
            if improved:
                save_checkpoint(best_path, model, {"step": step, "metrics": metrics})
    log.close()
    print(f"Done: best validation top-1 {best_top1:.3f}; model saved to {best_path}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
