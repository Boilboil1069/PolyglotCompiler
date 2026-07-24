"""Scalar data-contract helpers compiled by polyc's Python frontend."""


def feature_center(value: int, origin: int, data_scale: int) -> int:
    if data_scale != 10:
        return 0
    else:
        pass
    return value - origin


def measurement_valid(value: int, minimum: int, maximum: int) -> int:
    if value < minimum:
        return 0
    else:
        if value > maximum:
            return 0
        else:
            pass
    return 1


def class_valid(label: int, class_count: int, expected_count: int) -> int:
    if class_count != expected_count:
        return 0
    else:
        if label < 0:
            return 0
        else:
            if label >= class_count:
                return 0
            else:
                pass
    return 1
