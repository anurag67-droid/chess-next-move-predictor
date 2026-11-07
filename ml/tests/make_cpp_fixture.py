"""Writes the small model that tests/move_net_test.cpp checks the C++ MoveNet against.

    cd ml
    python tests/make_cpp_fixture.py

A tiny network with random weights, batch-norm statistics and FiLM layers (training starts FiLM at
zero, which would leave the context untested), exported to tests/data/tiny.movenet, plus PyTorch's
predictions for the reference positions in tests/data/tiny-reference.txt.
"""

import sys
from pathlib import Path

import torch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from chessml.export import export, write_reference
from chessml.model import PRESETS, MoveNet

OUT = Path(__file__).resolve().parents[2] / "tests" / "data"

torch.manual_seed(0)
model = MoveNet(PRESETS["tiny"])
with torch.no_grad():
    for module in model.modules():
        if isinstance(module, torch.nn.BatchNorm2d):
            module.weight.uniform_(0.5, 1.5)
            module.bias.uniform_(-0.2, 0.2)
            module.running_mean.uniform_(-0.2, 0.2)
            module.running_var.uniform_(0.5, 1.5)
    for block in model.blocks:
        block.film.weight.normal_(0, 0.3)
        block.film.bias.normal_(0, 0.1)
    model.policy[3].weight.mul_(4)  # sharper move probabilities, so ranking mistakes show
model.eval()

export(model, OUT / "tiny.movenet")
write_reference(model, OUT / "tiny-reference.txt")
print(f"Wrote {OUT / 'tiny.movenet'} and {OUT / 'tiny-reference.txt'}")
