#!/usr/bin/env python3
"""Rebuild the deterministic integer Iris stream from the UCI source copy.

This is a provenance/developer utility, not part of the demo build.  The
compiled application reads the committed integer stream and never launches
CPython or any other external runtime.
"""

from __future__ import annotations

import csv
import hashlib
from decimal import Decimal
from pathlib import Path


PROJECT_DIR = Path(__file__).resolve().parent.parent
SOURCE = PROJECT_DIR / "data" / "source" / "bezdekIris.data"
OUTPUT = PROJECT_DIR / "data" / "iris_q10.csv"
EXPECTED_SHA256 = "0fed2a99db77ec533a62dc66894d3ec6df3b58b6a8f3cf4a6b47e4086b7f97dc"
LABELS = {
    "Iris-setosa": 0,
    "Iris-versicolor": 1,
    "Iris-virginica": 2,
}


def q10(value: str) -> int:
    scaled = Decimal(value) * 10
    if scaled != scaled.to_integral_value():
        raise ValueError(f"Iris value is not exactly representable in Q10: {value}")
    return int(scaled)


def main() -> None:
    raw = SOURCE.read_bytes()
    digest = hashlib.sha256(raw).hexdigest()
    if digest != EXPECTED_SHA256:
        raise RuntimeError(f"unexpected UCI source digest: {digest}")

    rows: list[tuple[int, int, int, int, int]] = []
    for fields in csv.reader(raw.decode("ascii").splitlines()):
        if not fields:
            continue
        if len(fields) != 5 or fields[4] not in LABELS:
            raise ValueError(f"invalid Iris row: {fields!r}")
        rows.append((*map(q10, fields[:4]), LABELS[fields[4]]))
    if len(rows) != 150:
        raise RuntimeError(f"expected 150 Iris rows, got {len(rows)}")

    # UCI stores fifty consecutive rows per class.  Interleave each split so
    # every 12-row training minibatch contains four rows from every class.
    ordered = []
    for within_class in range(40):
        ordered.extend(rows[label * 50 + within_class] for label in range(3))
    for within_class in range(40, 50):
        ordered.extend(rows[label * 50 + within_class] for label in range(3))

    with OUTPUT.open("w", encoding="ascii", newline="") as stream:
        writer = csv.writer(stream, lineterminator="\n")
        writer.writerow(
            ["sepal_length_scaled", "sepal_width_scaled", "petal_length_scaled",
             "petal_width_scaled", "class_label"]
        )
        writer.writerows(ordered)

    print(f"wrote {len(ordered)} deterministic Q10 rows to {OUTPUT}")


if __name__ == "__main__":
    main()
