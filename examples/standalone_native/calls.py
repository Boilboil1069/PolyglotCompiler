def twice(x: int) -> int:
    return x * 2

def combine(x: int, y: int) -> int:
    return twice(x) + twice(y)

def main() -> int:
    return combine(13, 8)
