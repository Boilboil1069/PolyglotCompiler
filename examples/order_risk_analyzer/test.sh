#!/usr/bin/env bash
set -euo pipefail

EXAMPLE_DIR="$(cd "$(dirname "$0")" && pwd)"
"$EXAMPLE_DIR/run.sh" > "$EXAMPLE_DIR/build/actual_output.txt"
diff -u "$EXAMPLE_DIR/expected_output.txt" "$EXAMPLE_DIR/build/actual_output.txt"
PYTHONDONTWRITEBYTECODE=1 PYTHONPATH="$EXAMPLE_DIR/python" \
  python3 -m unittest discover -s "$EXAMPLE_DIR/tests" -v
