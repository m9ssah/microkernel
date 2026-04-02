#!/usr/bin/env python3
"""Generate test shard files for distributed logistic regression training.

Creates shard_1.txt, shard_2.txt, shard_3.txt with synthetic binary
classification data. Each line: feature_1 feature_2 ... feature_N label
"""

import random
import sys

NUM_SHARDS = 3
SAMPLES_PER_SHARD = 50
NUM_FEATURES = 4

random.seed(42)

for shard_id in range(1, NUM_SHARDS + 1):
    filename = f"shard_{shard_id}.txt"
    with open(filename, "w") as f:
        f.write(f"# shard {shard_id}: {SAMPLES_PER_SHARD} samples, {NUM_FEATURES} features\n")
        for _ in range(SAMPLES_PER_SHARD):
            features = [random.gauss(0, 1) for _ in range(NUM_FEATURES)]
            # simple separable rule: label = 1 if weighted sum > 0
            score = 0.5 * features[0] + 0.3 * features[1] - 0.2 * features[2] + 0.1 * features[3]
            label = 1.0 if score > 0 else 0.0
            line = " ".join(f"{v:.4f}" for v in features) + f" {label:.1f}\n"
            f.write(line)
    print(f"wrote {filename} ({SAMPLES_PER_SHARD} samples)")
