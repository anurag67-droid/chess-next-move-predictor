"""MoveNet: predicts the move a human plays, given the position and who is playing.

A residual convolutional network over the 8x8 board. The context (the mover's rating, the opponent's
rating, the time control and the mover's clock) is embedded and fed to every residual block as a
per-channel scale and shift (FiLM), so the same position can get a 1200-style or a 2400-style answer.

Two heads:
  * policy: a score for each of the 1858 possible moves (the prediction). Convolutional, like Leela
    Chess Zero's: 73 move planes over the 64 from-squares, gathered into the 1858 moves.
  * value:  win / draw / loss for the mover (a helper task that improves the policy, and a useful extra)
"""

from __future__ import annotations

from dataclasses import asdict, dataclass

import torch
import torch.nn as nn
import torch.nn.functional as F

from .encoding import CLOCK_BUCKETS, ELO_BUCKETS, MOVE_PLANE_INDEX, MOVE_PLANES, NUM_PLANES, SPEEDS


@dataclass
class ModelConfig:
    channels: int = 128
    blocks: int = 10
    context_dim: int = 64

    def to_dict(self) -> dict:
        return asdict(self)


PRESETS = {
    "tiny": ModelConfig(channels=32, blocks=3, context_dim=16),  # tests
    "small": ModelConfig(channels=64, blocks=6, context_dim=32),  # CPU
    "medium": ModelConfig(channels=128, blocks=10, context_dim=64),  # a laptop GPU, overnight
    "large": ModelConfig(channels=192, blocks=15, context_dim=96),  # more data and time
}


class ResidualBlock(nn.Module):
    def __init__(self, channels: int, context_dim: int):
        super().__init__()
        self.conv1 = nn.Conv2d(channels, channels, 3, padding=1, bias=False)
        self.bn1 = nn.BatchNorm2d(channels)
        self.conv2 = nn.Conv2d(channels, channels, 3, padding=1, bias=False)
        self.bn2 = nn.BatchNorm2d(channels)
        self.film = nn.Linear(context_dim, 2 * channels)
        nn.init.zeros_(self.film.weight)  # starts as a plain residual block
        nn.init.zeros_(self.film.bias)

    def forward(self, x: torch.Tensor, context: torch.Tensor) -> torch.Tensor:
        scale, shift = self.film(context)[:, :, None, None].chunk(2, dim=1)
        y = F.relu(self.bn1(self.conv1(x)) * (1 + scale) + shift)
        y = self.bn2(self.conv2(y))
        return F.relu(x + y)


class MoveNet(nn.Module):
    def __init__(self, config: ModelConfig):
        super().__init__()
        self.config = config
        c, d = config.channels, config.context_dim
        self.elo_self = nn.Embedding(ELO_BUCKETS, d)
        self.elo_opp = nn.Embedding(ELO_BUCKETS, d)
        self.speed = nn.Embedding(len(SPEEDS), d)
        self.clock = nn.Embedding(CLOCK_BUCKETS, d)
        self.context = nn.Sequential(nn.ReLU(), nn.Linear(d, d), nn.ReLU())

        self.stem = nn.Sequential(nn.Conv2d(NUM_PLANES, c, 3, padding=1, bias=False), nn.BatchNorm2d(c), nn.ReLU())
        self.blocks = nn.ModuleList(ResidualBlock(c, d) for _ in range(config.blocks))

        self.policy = nn.Sequential(
            nn.Conv2d(c, c, 3, padding=1, bias=False), nn.BatchNorm2d(c), nn.ReLU(), nn.Conv2d(c, MOVE_PLANES, 1)
        )
        self.register_buffer("move_cells", torch.tensor(MOVE_PLANE_INDEX, dtype=torch.long), persistent=False)
        self.value = nn.Sequential(
            nn.Conv2d(c, 8, 1, bias=False),
            nn.BatchNorm2d(8),
            nn.ReLU(),
            nn.Flatten(),
            nn.Linear(8 * 64, 128),
            nn.ReLU(),
            nn.Linear(128, 3),  # loss, draw, win for the mover
        )

    def forward(
        self,
        planes: torch.Tensor,
        elo_self: torch.Tensor,
        elo_opp: torch.Tensor,
        speed: torch.Tensor,
        clock: torch.Tensor,
    ) -> tuple[torch.Tensor, torch.Tensor]:
        context = self.context(self.elo_self(elo_self) + self.elo_opp(elo_opp) + self.speed(speed) + self.clock(clock))
        x = self.stem(planes)
        for block in self.blocks:
            x = block(x, context)
        policy = self.policy(x).flatten(1)[:, self.move_cells]  # (B, 73 * 64) -> (B, 1858)
        return policy, self.value(x)


def count_parameters(model: nn.Module) -> int:
    return sum(p.numel() for p in model.parameters())


def save_checkpoint(path, model: MoveNet, extra: dict | None = None) -> None:
    torch.save({"config": model.config.to_dict(), "model": model.state_dict(), **(extra or {})}, path)


def load_model(path, device: torch.device | str = "cpu") -> tuple[MoveNet, dict]:
    checkpoint = torch.load(path, map_location=device, weights_only=False)
    model = MoveNet(ModelConfig(**checkpoint["config"])).to(device)
    model.load_state_dict(checkpoint["model"])
    model.eval()
    return model, checkpoint
