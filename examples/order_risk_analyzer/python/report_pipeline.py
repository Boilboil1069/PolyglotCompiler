"""CSV ingestion, validation, cross-language evaluation, and report rendering."""

from __future__ import annotations

import csv
import json
from dataclasses import asdict, dataclass
from decimal import Decimal, InvalidOperation
from pathlib import Path
from typing import Iterable

from risk_bridge import RiskKernel


@dataclass(frozen=True)
class Order:
    order_id: str
    amount: Decimal
    items: int
    distance_km: float
    customer_days: int
    chargebacks: int


@dataclass(frozen=True)
class EvaluatedOrder:
    order_id: str
    amount: str
    risk_score: int
    action: str
    shipping_cost: str


def load_orders(path: str | Path) -> list[Order]:
    orders: list[Order] = []
    with Path(path).open(newline="", encoding="utf-8") as stream:
        for line_number, row in enumerate(csv.DictReader(stream), start=2):
            try:
                order = Order(
                    order_id=row["order_id"].strip(),
                    amount=Decimal(row["amount"]),
                    items=int(row["items"]),
                    distance_km=float(row["distance_km"]),
                    customer_days=int(row["customer_days"]),
                    chargebacks=int(row["chargebacks"]),
                )
            except (InvalidOperation, KeyError, TypeError, ValueError) as exc:
                raise ValueError(f"invalid CSV row {line_number}: {exc}") from exc
            if not order.order_id:
                raise ValueError(f"invalid CSV row {line_number}: empty order_id")
            orders.append(order)
    if not orders:
        raise ValueError("input CSV contains no orders")
    return orders


def analyze(orders: Iterable[Order], kernel: RiskKernel) -> list[EvaluatedOrder]:
    result: list[EvaluatedOrder] = []
    for order in orders:
        decision = kernel.evaluate(
            amount=float(order.amount),
            items=order.items,
            distance_km=order.distance_km,
            customer_days=order.customer_days,
            chargebacks=order.chargebacks,
        )
        result.append(
            EvaluatedOrder(
                order_id=order.order_id,
                amount=f"{order.amount:.2f}",
                risk_score=decision.score,
                action=decision.action,
                shipping_cost=f"{decision.shipping_cost:.2f}",
            )
        )
    return result


def summary(rows: Iterable[EvaluatedOrder]) -> dict[str, object]:
    materialized = list(rows)
    return {
        "orders": len(materialized),
        "amount": f"{sum(Decimal(row.amount) for row in materialized):.2f}",
        "shipping": f"{sum(Decimal(row.shipping_cost) for row in materialized):.2f}",
        "actions": {
            action: sum(row.action == action for row in materialized)
            for action in ("APPROVE", "REVIEW", "HIGH")
        },
    }


def write_json(path: str | Path, kernel: RiskKernel, rows: list[EvaluatedOrder]) -> None:
    payload = {"kernel": kernel.version, "summary": summary(rows), "orders": [asdict(row) for row in rows]}
    Path(path).write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")
