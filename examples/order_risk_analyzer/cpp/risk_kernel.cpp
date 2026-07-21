#include "risk_kernel.h"

#include <algorithm>
#include <cmath>

namespace {

double RoundMoney(double value) { return std::round(value * 100.0) / 100.0; }

bool IsValid(const RiskOrder &order) {
  return std::isfinite(order.amount) && order.amount >= 0.0 && order.items > 0 &&
         std::isfinite(order.distance_km) && order.distance_km >= 0.0 &&
         order.customer_days >= 0 && order.chargebacks >= 0;
}

}  // namespace

extern "C" int risk_evaluate(const RiskOrder *order, RiskDecision *decision) {
  if (order == nullptr || decision == nullptr) return -1;
  if (!IsValid(*order)) return -2;

  int score = order->chargebacks * 32;
  if (order->amount >= 1000.0) {
    score += 22;
  } else if (order->amount >= 500.0) {
    score += 12;
  } else if (order->amount >= 200.0) {
    score += 5;
  }

  if (order->customer_days < 30) {
    score += 20;
  } else if (order->customer_days < 90) {
    score += 12;
  }
  if (order->distance_km >= 200.0) {
    score += 10;
  } else if (order->distance_km >= 100.0) {
    score += 5;
  }
  if (order->items >= 6) score += 4;
  if (order->amount / order->items >= 250.0) score += 6;

  decision->score = std::min(score, 100);
  double shipping = 4.25 + order->distance_km * 0.09 + order->items * 0.40;
  if (decision->score >= 70) {
    shipping *= 1.25;
  } else if (decision->score >= 15) {
    shipping *= 1.10;
  }
  decision->shipping_cost = RoundMoney(shipping);
  return 0;
}

extern "C" const char *risk_kernel_version(void) { return "risk-kernel/1.0"; }
