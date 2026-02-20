# ============================================================================
# visualizer.py — Python charting / summary functions
# Compiled by PolyglotCompiler's frontend_python → shared IR
# ============================================================================


def generate_label(source: str) -> str:
    """Generate a display label from a file path."""
    name = source.rsplit("/", 1)[-1].rsplit(".", 1)[0]
    return name.replace("_", " ").title()


def plot_signal(data: list, title: str) -> str:
    """
    Produce an ASCII-art mini-chart of the signal (stub).
    Returns a summary string instead of opening a GUI window.
    """
    if not data:
        return f"[{title}] (empty signal)"
    mn = min(data)
    mx = max(data)
    rng = mx - mn if mx != mn else 1.0
    width = 60
    step = max(1, len(data) // 20)
    lines = [title, "-" * width]
    for i in range(0, len(data), step):
        bar_len = int((data[i] - mn) / rng * (width - 2))
        lines.append("|" + "#" * bar_len)
    avg = sum(data) / len(data)
    lines.append(f"samples={len(data)}, min={mn:.4f}, max={mx:.4f}, avg={avg:.4f}")
    return "\n".join(lines)
