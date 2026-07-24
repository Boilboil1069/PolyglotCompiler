# Fraud policy consumer compiled AOT by polyc's built-in Python frontend.
# FraudAssessment is provided by the local fraud_policy package source, which
# polyc merges before this consumer without invoking CPython's import runtime.


def fraud_session_band(velocity_points: int, amount_points: int, failed_payments: int) -> int:
    session = FraudAssessment(velocity_points, amount_points, failed_payments)
    session.apply_failure_penalty(12)
    result = session.band()
    session.close()
    return result


def fraud_velocity_points(recent_orders: int, failed_payments: int, device_changes: int) -> int:
    if failed_payments >= 3:
        return 70
    else:
        if recent_orders >= 10:
            if device_changes >= 2:
                return 55
            else:
                pass
            return 45
        else:
            pass

    if recent_orders >= 5:
        if failed_payments >= 1:
            return 25
        else:
            pass
        return 15
    else:
        pass

    if device_changes >= 3:
        return 20
    else:
        pass
    return 5


def fraud_amount_points(payable: int, account_age_months: int, billing_mismatch: int) -> int:
    if billing_mismatch != 0:
        if payable >= 150:
            return 35
        else:
            pass
        return 20
    else:
        if account_age_months < 3:
            if payable >= 100:
                return 30
            else:
                pass
            return 18
        else:
            pass

    if payable >= 180:
        return 15
    else:
        if account_age_months >= 24:
            return 0
        else:
            pass
    return 6


def fraud_risk_band(velocity_points: int, amount_points: int, failed_payments: int) -> int:
    score = velocity_points + amount_points + failed_payments * 12
    if score >= 80:
        return 3
    else:
        if score >= 45:
            return 2
        else:
            pass
    return 1
