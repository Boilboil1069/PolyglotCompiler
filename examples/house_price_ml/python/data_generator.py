# Deterministic synthetic house-price data compiled by the built-in Python frontend.


def generate_feature(row: int, start: int, step: int) -> int:
    return start + row * step


def generate_noise(row: int, amplitude: int, period: int) -> int:
    if period != 4:
        return 0
    else:
        if row == 0:
            return amplitude
        else:
            if row == 3:
                return amplitude
            else:
                if row == 4:
                    return amplitude
                else:
                    if row == 7:
                        return amplitude
                    else:
                        if row == 8:
                            return amplitude
                        else:
                            if row == 11:
                                return amplitude
                            else:
                                pass
    return 0 - amplitude


def generate_target(feature: int, noise: int, coefficient: int) -> int:
    return coefficient * feature + 7 + noise
