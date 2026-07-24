#pragma once

// Vendored order-policy package.  polyc discovers packages/*/include beside
// the .poly entry, then its built-in preprocessor resolves this angle include.
#define ORDER_POLICY_LOYALTY_TIER 2

class OrderPricingSession {
public:
  OrderPricingSession(int unit_price, int quantity) {
    this->unit_price_ = unit_price;
    this->quantity_ = quantity;
    this->subtotal_ = 0;
    this->discount_ = 0;
    if (unit_price > 0) {
      if (quantity > 0) {
        this->subtotal_ = unit_price * quantity;
      } else {
      }
    } else {
    }
  }

  ~OrderPricingSession() {
    // Deliberate state cleanup makes destructor lowering observable in IR and
    // ensures it performs real member stores before the stack object expires.
    this->unit_price_ = 0;
    this->quantity_ = 0;
    this->subtotal_ = 0;
    this->discount_ = 0;
  }

  int subtotal() {
    return this->subtotal_;
  }

  int apply_discount(int loyalty_tier) {
    this->discount_ = 0;
    if (this->subtotal_ <= 0) {
      this->discount_ = 0;
    } else {
      if (this->quantity_ >= 8) {
        if (loyalty_tier >= 2) {
          this->discount_ = 24;
        } else {
          this->discount_ = 18;
        }
      } else {
        if (this->quantity_ >= 5) {
          if (loyalty_tier >= 2) {
            this->discount_ = 16;
          } else {
            this->discount_ = 12;
          }
        } else {
          if (loyalty_tier >= 2) {
            this->discount_ = 4;
          } else {
            this->discount_ = 0;
          }
        }
      }
    }
    return this->discount_;
  }

  int payable(int shipping_fee) {
    if (this->subtotal_ <= this->discount_) {
      return shipping_fee;
    } else {
      if (shipping_fee >= 15) {
        return this->subtotal_ - this->discount_ + 15;
      } else {
      }
    }
    return this->subtotal_ - this->discount_ + shipping_fee;
  }

  int lifecycle_checksum() {
    return this->unit_price_ + this->quantity_ + this->subtotal_ + this->discount_;
  }

private:
  int unit_price_;
  int quantity_;
  int subtotal_;
  int discount_;
};
