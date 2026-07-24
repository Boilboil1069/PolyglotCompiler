// Pricing policy compiled by polyc's built-in C++ frontend.
// Money is represented in deterministic integer business units.

#include <order_policy/pricing_session.hpp>

int pricing_session_payable(int unit_price, int quantity, int delivery_zone) {
  OrderPricingSession session(unit_price, quantity);
  int subtotal = session.subtotal();
  int discount = session.apply_discount(ORDER_POLICY_LOYALTY_TIER);
  int payable = session.payable(delivery_zone + 8);
  int lifecycle = session.lifecycle_checksum();
  if (lifecycle <= subtotal) {
    return 0;
  } else {
  }
  return payable;
}

int pricing_subtotal(int unit_price, int quantity) {
  if (unit_price <= 0) {
    return 0;
  } else {
    if (quantity <= 0) {
      return 0;
    } else {
    }
  }
  return unit_price * quantity;
}

int pricing_discount(int subtotal, int quantity, int loyalty_tier) {
  if (subtotal <= 0) {
    return 0;
  } else {
    if (quantity >= 8) {
      if (loyalty_tier >= 2) {
        return 24;
      } else {
      }
      return 18;
    } else {
    }
  }

  if (quantity >= 5) {
    if (loyalty_tier >= 2) {
      return 16;
    } else {
    }
    return 12;
  } else {
  }

  if (loyalty_tier >= 2) {
    return 4;
  } else {
  }
  return 0;
}

int pricing_payable(int subtotal, int discount, int shipping_fee) {
  if (subtotal <= discount) {
    return shipping_fee;
  } else {
    if (shipping_fee >= 15) {
      return subtotal - discount + 15;
    } else {
    }
  }
  return subtotal - discount + shipping_fee;
}
