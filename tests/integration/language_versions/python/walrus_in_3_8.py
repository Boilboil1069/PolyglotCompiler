# Fixture: Python source that requires the walrus operator (PEP 572).
# Walrus '`:=`' was introduced in Python 3.8 and the PolyglotCompiler
# Python frontend gates it on `--python-version`.

def consume(values):
    if (n := len(values)) > 5:
        return n
    return -1
