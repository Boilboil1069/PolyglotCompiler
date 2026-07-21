"""Typed ctypes bridge between Python orchestration and the C++ kernel."""

from __future__ import annotations

import ctypes
import os
import sys
from dataclasses import dataclass
from pathlib import Path


class _RiskOrder(ctypes.Structure):
    _fields_ = [
        ("amount", ctypes.c_double),
        ("items", ctypes.c_int),
        ("distance_km", ctypes.c_double),
        ("customer_days", ctypes.c_int),
        ("chargebacks", ctypes.c_int),
    ]


class _RiskDecision(ctypes.Structure):
    _fields_ = [("score", ctypes.c_int), ("shipping_cost", ctypes.c_double)]


@dataclass(frozen=True)
class Decision:
    score: int
    shipping_cost: float

    @property
    def action(self) -> str:
        if self.score >= 70:
            return "HIGH"
        if self.score >= 15:
            return "REVIEW"
        return "APPROVE"


def _default_library() -> Path:
    suffix = ".dll" if sys.platform == "win32" else ".dylib" if sys.platform == "darwin" else ".so"
    return Path(__file__).resolve().parents[1] / "build" / f"librisk_kernel{suffix}"


class RiskKernel:
    def __init__(self, library: str | Path | None = None) -> None:
        path = Path(library or os.environ.get("RISK_KERNEL_LIB", _default_library()))
        if not path.is_file():
            raise FileNotFoundError(f"C++ risk kernel not found: {path}; run ./run.sh first")
        self._library = ctypes.CDLL(str(path))
        self._library.risk_evaluate.argtypes = [
            ctypes.POINTER(_RiskOrder),
            ctypes.POINTER(_RiskDecision),
        ]
        self._library.risk_evaluate.restype = ctypes.c_int
        self._library.risk_kernel_version.argtypes = []
        self._library.risk_kernel_version.restype = ctypes.c_char_p

    @property
    def version(self) -> str:
        return self._library.risk_kernel_version().decode("ascii")

    def evaluate(
        self,
        *,
        amount: float,
        items: int,
        distance_km: float,
        customer_days: int,
        chargebacks: int,
    ) -> Decision:
        order = _RiskOrder(amount, items, distance_km, customer_days, chargebacks)
        result = _RiskDecision()
        status = self._library.risk_evaluate(ctypes.byref(order), ctypes.byref(result))
        if status != 0:
            raise ValueError(f"C++ kernel rejected order fields (status {status})")
        return Decision(result.score, result.shipping_cost)
