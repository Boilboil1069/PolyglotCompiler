#!/usr/bin/env python3
"""Compile and execute the public native runtime API in each supported frontend."""
import argparse
import json
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from differential_native import invoke
from corpus import LANGUAGES
from differential_corpus import local, statement, function, ret


def program(language, output_file):
    lines = [statement(language, expr) for expr in (
        'print_i64(args_count())', 'print_text(arg_text(1))', 'print_text("\\n")',
        'print_i64(arg_int(2, -999))', 'print_i64(arg_int(3, -999))',
        'print_i64(arg_int(4, -999))')]
    lines += [local(language, 'a', 'array_new(4)')]
    lines += [statement(language, expr) for expr in (
        'print_i64(array_len(a))', 'array_set(a, 0, 42)', 'array_set(a, 3, -7)',
        'print_i64(array_get(a, 0, -99))', 'print_i64(array_get(a, 3, -99))',
        'print_i64(array_get(a, 4, -99))', 'print_i64(array_set(a, -1, 77))',
        'print_i64(array_get(a, 0, -99))', 'array_free(a)',
        'print_i64(array_new(-1))')]
    lines += [local(language, 'fd', 'file_open_write(' + json.dumps(str(output_file)) + ')')]
    lines += [statement(language, expr) for expr in (
        'file_write_text(fd, "values\\n")', 'file_write_int(fd, 42, 10)',
        'file_write_int(fd, -7, 10)', 'file_write_int(fd, arg_int(2, 0), 10)',
        'file_close(fd)')]
    lines += [local(language, 'rd', 'file_open_ints(' + json.dumps(str(output_file)) + ')')]
    lines += [statement(language, expr) for expr in (
        'print_i64(file_next_int(rd, -999))', 'print_i64(file_next_int(rd, -999))',
        'print_i64(file_next_int(rd, -999))', 'print_i64(file_next_int(rd, -999))',
        'file_close(rd)')]
    lines += [ret(language, 'false' if language == 'javascript' else '0')]
    body = '\n'.join(lines)
    source = function(language, 'Main' if language == 'dotnet' else 'main', [], body)
    if language == 'javascript': source = source.replace('@returns {number}', '@returns {boolean}')
    if language in ('java', 'dotnet'): source = 'class Program {\n' + source + '}\n'
    if language == 'go': source = 'package main\n' + source
    return source


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--polyc', type=Path, default=ROOT / 'build-release/polyc')
    destination = parser.add_mutually_exclusive_group(required=True)
    destination.add_argument('--output', type=Path)
    destination.add_argument('--output-root', type=Path)
    parser.add_argument('--opt-levels', nargs='+', type=int, default=[0, 1, 2, 3])
    parser.add_argument('--allocators', nargs='+', default=['linear', 'graph'])
    parser.add_argument('--languages', nargs='+', default=list(LANGUAGES), choices=list(LANGUAGES))
    args = parser.parse_args()
    if args.output_root:
        args.output_root.mkdir(parents=True, exist_ok=True)
        work = Path(tempfile.mkdtemp(prefix='runtime-', dir=args.output_root.resolve()))
    else:
        work = args.output.resolve(); work.mkdir(parents=True, exist_ok=False)
    compiler = args.polyc.resolve()
    expected = '5\nhello\n-9223372036854775808\n-999\n-999\n4\n42\n-7\n-99\n-1\n42\n0\n42\n-7\n-9223372036854775808\n-999\n'
    expected_file = 'values\n42\n-7\n-9223372036854775808\n'
    rows = []
    for lang in args.languages:
        folder = work / lang; folder.mkdir()
        out = folder / 'roundtrip.txt'
        src = folder / ('runtime.' + LANGUAGES[lang]); src.write_text(program(lang, out))
        for opt in args.opt_levels:
            for alloc in args.allocators:
                exe = folder / f'runtime-O{opt}-{alloc}'
                row = {'language': lang, 'opt': opt, 'allocator': alloc, 'passed': False}
                row['compile'] = invoke([compiler, '--strict', '--no-package-index', '--no-aux', '--quiet',
                    f'-O{opt}', '--regalloc=' + alloc, '-o', exe, src], folder)
                if row['compile']['returncode'] == 0:
                    if out.exists(): out.unlink()
                    row['run'] = invoke([exe, 'hello', '-9223372036854775808', '9223372036854775808', '12x'], folder, 10)
                    row['file_contents'] = out.read_text() if out.exists() else None
                    row['passed'] = (row['run']['returncode'] == 0 and row['run']['stdout'] == expected
                                     and row['run']['stderr'] == '' and row['file_contents'] == expected_file)
                rows.append(row)
                print(f'{lang} O{opt} {alloc}: {"PASS" if row["passed"] else "FAIL"}', flush=True)
                (work / 'results.json').write_text(json.dumps({'expected_stdout': expected, 'cases': rows}, indent=2) + '\n')
    print(f'{sum(r["passed"] for r in rows)}/{len(rows)} runtime cases passed; artifacts: {work}')
    return 0 if all(r['passed'] for r in rows) else 1

if __name__ == '__main__':
    raise SystemExit(main())
