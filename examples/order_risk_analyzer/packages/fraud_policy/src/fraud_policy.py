class FraudAssessment:
    """Deterministic integer fraud state compiled without CPython.

    The built-in Python frontend lowers this class to stack storage plus a
    four-field native aggregate.  Cleanup is explicit: this subset does not
    claim garbage collection, finalizer scheduling, or descriptor semantics.
    """

    def __init__(self, velocity_points: int, amount_points: int, failed_payments: int):
        self.velocity_points = velocity_points
        self.amount_points = amount_points
        self.failed_payments = failed_payments
        self.score = velocity_points + amount_points

    def apply_failure_penalty(self, multiplier: int) -> int:
        self.score += self.failed_payments * multiplier
        return self.score

    def band(self) -> int:
        if self.score >= 80:
            return 3
        else:
            if self.score >= 45:
                return 2
            else:
                pass
        return 1

    def close(self) -> None:
        self.velocity_points = 0
        self.amount_points = 0
        self.failed_payments = 0
        self.score = 0
