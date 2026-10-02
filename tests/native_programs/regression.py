#!/usr/bin/env python3
"""Host-native regression; retained sources/logs make failures reproducible."""
import argparse
import json
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--polyc', type=Path, required=True)
    parser.add_argument('--output-root', type=Path, required=True)
    args = parser.parse_args()
    args.output_root.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='native regression ', dir=args.output_root.resolve()))
    compiler = args.polyc.resolve()
    print(f'Retained native regression artifacts: {work}', flush=True)
    result = subprocess.run([sys.executable, str(ROOT / 'scripts/benchmark_native.py'),
                             '--polyc', str(compiler), '--output', str(work / 'matrix'),
                             '--repetitions', '1', '--warmups', '0', '--scales', '8'], timeout=240)
    if result.returncode: return result.returncode
    records = []
    def check(name, source, expected=None, diagnostic=None, flags=(), stdout='', extension='poly'):
        src = work / (name + '.' + extension); src.write_text(source)
        exe = work / name
        cmd = [str(compiler), '--strict', '--no-package-index', '--no-aux', '--quiet', '-o', str(exe), str(src), *flags]
        c = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
        record = {'name': name, 'command': cmd, 'compile_exit': c.returncode, 'stdout': c.stdout, 'stderr': c.stderr}
        records.append(record)
        (work / 'checks.json').write_text(json.dumps(records, indent=2))
        if diagnostic:
            assert c.returncode != 0 and diagnostic in c.stderr + c.stdout, record
            assert not exe.exists(), record
        else:
            assert c.returncode == 0 and exe.exists(), record
            if expected is not None:
                r = subprocess.run([str(exe)], capture_output=True, text=True, timeout=10)
                record['run'] = {'exit': r.returncode, 'stdout': r.stdout, 'stderr': r.stderr}
                assert (r.returncode, r.stdout, r.stderr) == (expected, stdout, ''), record
    check('missing entry', 'FUNC helper() -> INT { RETURN 42; }', diagnostic='no executable entry')
    check('custom entry', 'FUNC answer() -> INT { RETURN 42; }', expected=42, flags=('--entry=answer',))
    check('invalid entry', 'FUNC main(x: INT) -> INT { RETURN x; }', diagnostic='must have no parameters')
    check('object only', 'FUNC helper() -> INT { RETURN 42; }', flags=('-c',))
    for opt in range(4):
        check(f'print loop O{opt}', '''FUNC main() -> INT {
VAR i = 0;
WHILE i < 3 { PRINTLN "loop"; i = i + 1; }
IF i == 99 { PRINTLN "unreachable"; }
PRINTLN "done"; RETURN 42;
}''', expected=42, stdout='loop\nloop\nloop\ndone\n', flags=(f'-O{opt}',))
    check('too many arguments', 'FUNC helper(a: INT, b: INT, c: INT, d: INT, e: INT, f: INT, g: INT, h: INT, i: INT) -> INT { RETURN i; } FUNC main() -> INT { RETURN helper(1,2,3,4,5,6,7,8,9); }', diagnostic='more than eight integer')
    # Compound updates must write through the same loop-carried storage.
    for opt in range(4):
        check(f'augmented python O{opt}', 'def main() -> int:\n    x = 0\n    while x < 42:\n        x += 1\n    return x\n', expected=42, extension='py', flags=(f'-O{opt}',))
        for extension, entry in [('java', 'main'), ('cs', 'Main')]:
            check(f'augmented {extension} O{opt}', f'class Program {{ static int {entry}() {{ int x = 0; while (x < 42) {{ x += 1; }} return x; }} }}', expected=42, extension=extension, flags=(f'-O{opt}',))
        check(f'augmented cpp O{opt}', 'int main() { int x = 0; while (x < 42) { x += 1; } return x; }', expected=42, extension='cpp', flags=(f'-O{opt}',))
    (work / 'checks.json').write_text(json.dumps(records, indent=2))
    print('Native matrix and entry/output checks passed.', flush=True)
    return 0

if __name__ == '__main__':
    raise SystemExit(main())
