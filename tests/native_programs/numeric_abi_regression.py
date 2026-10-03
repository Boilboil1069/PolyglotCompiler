#!/usr/bin/env python3
"""Execute Poly -> C++ scalar ABI conversions for all supported ARM64 modes."""
import argparse
import json
import pathlib
import subprocess
import sys

SOURCE = pathlib.Path(__file__).resolve().parent / 'mixed_numeric_abi' / 'casts.poly'
EXPECTED = ('-0x1.0000000000000p+31\n0x1.0000000000000p+32\n'
            '-0x1.0000000000000p+63\n0x1.0000000000000p+64\n'
            '0x1.0000000000000p+24\n0x1.0000000000000p+53\n'
            '-0x0.0000000000000p+0\n9007199254740993\n0x1.0000000000000p+64\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', type=pathlib.Path, required=True)
    parser.add_argument('--output', type=pathlib.Path, required=True)
    options = parser.parse_args()
    compiler = options.compiler.resolve()
    output = options.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    rows = []
    for opt in range(4):
        for allocator in ('linear', 'graph'):
            executable = output / f'numeric-O{opt}-{allocator}'
            command = [str(compiler), '--strict', '--no-package-index', '--no-aux', '--quiet',
                       f'-O{opt}', f'--regalloc={allocator}', '-o', str(executable), str(SOURCE)]
            row = dict(opt=opt, allocator=allocator, command=command, passed=False)
            try:
                compile_run = subprocess.run(command, text=True, capture_output=True, timeout=60)
                row.update(compile_exit=compile_run.returncode,
                           compile_stdout=compile_run.stdout, compile_stderr=compile_run.stderr)
                if compile_run.returncode == 0:
                    run = subprocess.run([str(executable)], text=True, capture_output=True, timeout=10)
                    row.update(exit=run.returncode, stdout=run.stdout, stderr=run.stderr)
                    row['passed'] = run.returncode == 0 and run.stdout == EXPECTED and run.stderr == ''
            except subprocess.TimeoutExpired as error:
                row['error'] = str(error)
            rows.append(row)
            print(f'O{opt} {allocator}: {"PASS" if row["passed"] else "FAIL"}', flush=True)
            (output / 'results.json').write_text(json.dumps({'expected_stdout': EXPECTED, 'rows': rows}, indent=2))
    return 0 if all(row['passed'] for row in rows) else 1


if __name__ == '__main__':
    sys.exit(main())
