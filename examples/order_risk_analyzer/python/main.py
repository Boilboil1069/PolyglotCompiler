#!/usr/bin/env python3
from __future__ import annotations

import argparse
from pathlib import Path

from report_pipeline import analyze, load_orders, summary, write_json
from risk_bridge import RiskKernel


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description="Analyze order risk with Python + C++")
    parser.add_argument("input", nargs="?", type=Path, default=root / "data" / "orders.csv")
    parser.add_argument("--json", type=Path, default=root / "build" / "report.json")
    args = parser.parse_args()

    kernel = RiskKernel()
    rows = analyze(load_orders(args.input), kernel)
    totals = summary(rows)
    write_json(args.json, kernel, rows)

    print(f"Order risk report ({kernel.version})")
    for row in rows:
        print(
            f"{row.order_id}  score={row.risk_score:3d}  action={row.action:<7} "
            f"shipping=${row.shipping_cost}"
        )
    actions = totals["actions"]
    print(
        f"Summary: orders={totals['orders']} amount=${totals['amount']} "
        f"shipping=${totals['shipping']} approve={actions['APPROVE']} "
        f"review={actions['REVIEW']} high={actions['HIGH']}"
    )
    try:
        display_path = args.json.resolve().relative_to(root)
    except ValueError:
        display_path = args.json
    print(f"JSON: {display_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
