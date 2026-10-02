/**
 * Calculate the gross order amount in integer cents.
 * @param quantity Number of items in the order.
 * @param unit_cents Price of one item, in cents.
 * @return Gross amount in cents, before discounts.
 */
int subtotal(int quantity, int unit_cents) {
    return quantity * unit_cents;
}
