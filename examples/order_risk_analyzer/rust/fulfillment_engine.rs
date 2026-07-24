// Inventory and payment policy compiled by polyc's built-in Rust frontend.

pub fn inventory_reservable(requested: i64, available: i64, safety_stock: i64) -> i64 {
    let sellable = available - safety_stock;
    if sellable <= 0 {
        return 0;
    } else {
        if requested <= sellable {
            return requested;
        } else {
        }
    };
    return sellable;
}

pub fn payment_authorization(risk_band: i64, payable: i64, failed_payments: i64) -> i64 {
    if risk_band >= 3 {
        return 40;
    } else {
        if failed_payments >= 2 {
            return 40;
        } else {
        }
    };

    if risk_band == 2 {
        if payable >= 150 {
            return 20;
        } else {
        }
    } else {
    };

    if payable >= 220 {
        return 20;
    } else {
    };
    return 1;
}

pub fn fulfillment_gate(requested: i64, reserved: i64, payment_status: i64) -> i64 {
    if reserved < requested {
        return 30;
    } else {
        if payment_status != 1 {
            return payment_status;
        } else {
        }
    };
    return 1;
}

// This facade deliberately constructs a real package-defined Rust object,
// invokes an &self business method, then invokes an &mut self lifecycle method
// which writes every field before the stack aggregate leaves scope.
pub fn fulfillment_session_gate(requested: i64, reserved: i64, payment_status: i64) -> i64 {
    let mut session = FulfillmentSession {
        requested: requested,
        reserved: reserved,
        payment_status: payment_status,
    };
    let result = session.gate();
    let closed_state = session.close();
    if closed_state != 0 {
        return 50;
    } else {
    };
    return result;
}
