def cents_to_dollars(cents: int) -> float:
    """Convert an integer number of cents into a decimal dollar amount.

    The return value is for presentation; keep accounting arithmetic in cents.
    """
    return cents / 100.0
