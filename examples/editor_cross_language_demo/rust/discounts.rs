/// Apply a whole-percent discount to an amount in cents.
/// Integer division rounds the discounted amount down to whole cents.
/// gross_cents: gross order amount; percent: discount from 0 through 100.
pub fn apply_discount(gross_cents: i64, percent: i64) -> i64 {
    gross_cents * (100 - percent) / 100
}
