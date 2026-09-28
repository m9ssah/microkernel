#!/usr/bin/env python3
"""Generate test shard files for distributed logistic regression training.

LAST shard belongs to the FreeRTOS firmware worker, which has no filesystem.
It is emitted a second time as a C header (firmware/shard_data.h) so the exact
same numbers get baked into flash. Both come from this one generator with one
seed, so the firmware and the Linux workers can never disagree on the shard data.
"""

import random

NUM_SHARDS = 4
SAMPLES_PER_SHARD = 50
NUM_FEATURES = 4
FIRMWARE_HEADER = "firmware/shard_data.h"

random.seed(42)


def make_shard():
    """One shard's worth of rows, each row = NUM_FEATURES floats + a 0/1 label."""
    rows = []
    for _ in range(SAMPLES_PER_SHARD):
        features = [random.gauss(0, 1) for _ in range(NUM_FEATURES)]
        score = (0.5 * features[0] + 0.3 * features[1]
                 - 0.2 * features[2] + 0.1 * features[3])
        label = 1.0 if score > 0 else 0.0
        rows.append((features, label))
    return rows


# Formatting note: every value is written with exactly 4 decimal places, in the
# text file AND in the C header. strtof("0.1234") and the literal 0.1234f round
# to the identical IEEE-754 float, so the firmware's in-flash copy is bit-for-bit
# what a Linux worker would have parsed from the text file.
def fmt(v):
    return f"{v:.4f}"


shards = {}
for shard_id in range(1, NUM_SHARDS + 1):
    rows = make_shard()
    shards[shard_id] = rows

    filename = f"shard_{shard_id}.txt"
    with open(filename, "w") as f:
        f.write(f"# shard {shard_id}: {SAMPLES_PER_SHARD} samples, {NUM_FEATURES} features\n")
        for features, label in rows:
            f.write(" ".join(fmt(v) for v in features) + f" {label:.1f}\n")
    print(f"wrote {filename} ({SAMPLES_PER_SHARD} samples)")


# ---- emit the firmware's shard as a C header -------------------------------
fw_id = NUM_SHARDS
rows = shards[fw_id]

with open(FIRMWARE_HEADER, "w") as f:
    f.write("#ifndef SHARD_DATA_H\n#define SHARD_DATA_H\n\n")
    f.write(f"#define SHARD_ID          {fw_id}u\n")
    f.write(f"#define SHARD_N_SAMPLES   {SAMPLES_PER_SHARD}u\n")
    f.write(f"#define SHARD_N_FEATURES  {NUM_FEATURES}u\n\n")
    f.write("static const float g_shard_data[SHARD_N_SAMPLES * (SHARD_N_FEATURES + 1)] = {\n")
    for features, label in rows:
        vals = ", ".join(f"{fmt(v)}f" for v in features)
        f.write(f"    {vals}, {label:.1f}f,\n")
    f.write("};\n\n#endif /* SHARD_DATA_H */\n")

print(f"wrote {FIRMWARE_HEADER} (shard {fw_id} into firmware flash)")
