// Vendored Rust policy package.  polyc merges this source into the importing
// Rust compilation unit; no rustc, Cargo, registry, or downloaded runtime is
// involved.

pub struct FulfillmentSession {
    requested: i64,
    reserved: i64,
    payment_status: i64,
}

impl FulfillmentSession {
    // Rust named-field construction is the object's constructor path in this
    // static AOT subset.  The methods below exercise both shared and mutable
    // receiver ABIs against the same stack-resident aggregate.
    pub fn gate(&self) -> i64 {
        if self.reserved < self.requested {
            return 30;
        } else {
            if self.payment_status != 1 {
                return self.payment_status;
            } else {
            }
        };
        return 1;
    }

    pub fn close(&mut self) -> i64 {
        self.requested = 0;
        self.reserved = 0;
        self.payment_status = 0;
        return self.requested + self.reserved + self.payment_status;
    }
}
