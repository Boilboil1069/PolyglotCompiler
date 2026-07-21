from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from report_pipeline import analyze, load_orders, summary
from risk_bridge import RiskKernel


ROOT = Path(__file__).resolve().parents[1]


class PipelineTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.kernel = RiskKernel()

    def test_real_fixture_crosses_cpp_boundary(self) -> None:
        rows = analyze(load_orders(ROOT / "data" / "orders.csv"), self.kernel)
        self.assertEqual([row.risk_score for row in rows], [0, 75, 4, 17, 100, 0])
        self.assertEqual(summary(rows)["actions"], {"APPROVE": 3, "REVIEW": 1, "HIGH": 2})
        self.assertEqual(summary(rows)["shipping"], "93.12")

    def test_invalid_business_values_are_rejected_by_cpp(self) -> None:
        with self.assertRaisesRegex(ValueError, "status -2"):
            self.kernel.evaluate(
                amount=10.0,
                items=0,
                distance_km=1.0,
                customer_days=10,
                chargebacks=0,
            )

    def test_malformed_csv_reports_source_line(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "bad.csv"
            path.write_text(
                "order_id,amount,items,distance_km,customer_days,chargebacks\n"
                "broken,abc,1,2,3,0\n",
                encoding="utf-8",
            )
            with self.assertRaisesRegex(ValueError, "invalid CSV row 2"):
                load_orders(path)


if __name__ == "__main__":
    unittest.main()
