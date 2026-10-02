#!/usr/bin/env python3
"""Compare ARM64 stack, linear-scan and graph-coloring on checked long kernels.

Each strategy uses the same compiler, IR passes, linker and source. The stack
mode is the current correct emitter with register allocation disabled, not an
old compiler revision. Run only while the host is otherwise idle. Timings
include process startup; an empty program reports that floor separately.
"""
import argparse
import datetime
import hashlib
import json
from pathlib import Path
import platform
import random
import statistics
import sys

from differential_native import ROOT, invoke


def sources(loop_iterations, call_iterations, fibonacci):
    a, b = 0, 1
    for _ in range(fibonacci): a, b = b, a + b
    return {
        'startup': ('int main() { print_i64(0); return 0; }\n', 0),
        'integer_loop': (f'''long sum(long n) {{
  long i = 0; long total = 0;
  while (i < n) {{ total = total + i; i = i + 1; }}
  return total;
}}
int main() {{ print_i64(sum({loop_iterations})); return 0; }}
''', loop_iterations * (loop_iterations - 1) // 2),
        'recursive_calls': (f'''long fib(long n) {{
  if (n <= 1) {{ return n; }}
  return fib(n - 1) + fib(n - 2);
}}
int main() {{ print_i64(fib({fibonacci})); return 0; }}
''', a),
        'eight_arguments': (f'''long weighted(long a, long b, long c, long d, long e, long f, long g, long h) {{
  return a * 2 + b * 3 + c * 4 + d * 5 + e * 6 + f * 7 + g * 8 + h * 9;
}}
long calculate(long n) {{
  long i = 0; long total = 0;
  while (i < n) {{
    total = total + weighted(i, i+1, i+2, i+3, i+4, i+5, i+6, i+7);
    i = i + 1;
  }}
  return total;
}}
int main() {{ print_i64(calculate({call_iterations})); return 0; }}
''', 44 * call_iterations * (call_iterations - 1) // 2 + 196 * call_iterations),
    }


def fingerprints(compiler):
    paths = [compiler, compiler.with_name('polyld')]
    paths += [p for p in compiler.parent.iterdir() if p.suffix in ('.dylib', '.so', '.dll')]
    return {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(paths) if p.is_file()}


def save(output, report):
    (output / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
    lines = ['# Native optimization and register allocation measurements', '',
             'Stack = the same current ARM64 emitter with register allocation disabled. All outputs are checked exactly.',
             'Run times include process startup. The startup row reports that overhead independently. No build ran concurrently by protocol.', '',
             '| Kernel | Opt | Strategy | Result | Median ms | Min ms | Max ms | Stack / strategy | Bytes |',
             '| --- | --- | --- | --- | ---: | ---: | ---: | ---: | ---: |']
    baseline = {(row['kernel'], row['opt']): row.get('median_ms') for row in report['cases'] if row['allocator'] == 'stack'}
    for row in report['cases']:
        ratio = baseline.get((row['kernel'], row['opt']))
        ratio = ratio / row['median_ms'] if ratio is not None and row.get('median_ms') else None
        row['stack_to_strategy_ratio'] = ratio
        def number(value): return f'{value:.3f}' if value is not None else '—'
        lines.append(f"| {row['kernel']} | O{row['opt']} | {row['allocator']} | {row['status']} | {number(row.get('median_ms'))} | {number(row.get('min_ms'))} | {number(row.get('max_ms'))} | {number(ratio)} | {row.get('bytes', '—')} |")
    lines += ['', 'A ratio above 1 means this strategy ran faster than stack for that kernel and optimization level.',
              'These are host/workload observations, not a guarantee for other programs. Compile time is recorded as one sample per artifact.',
              'The compiler and shared-library hashes are checked again after measurement; a changed toolchain invalidates the run.']
    (output / 'report.md').write_text('\n'.join(lines) + '\n')
    (output / 'results.json').write_text(json.dumps(report, indent=2) + '\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--polyc', type=Path, default=ROOT / 'build-release/polyc')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--repetitions', type=int, default=7)
    parser.add_argument('--warmups', type=int, default=1)
    parser.add_argument('--loop-iterations', type=int, default=30000000)
    parser.add_argument('--call-iterations', type=int, default=1000000)
    parser.add_argument('--fibonacci', type=int, default=32)
    parser.add_argument('--opt-levels', nargs='+', choices=range(4), type=int, default=[0, 2, 3])
    parser.add_argument('--timeout', type=float, default=60)
    args = parser.parse_args()
    if args.repetitions < 1 or args.warmups < 0 or args.loop_iterations < 1 or args.call_iterations < 1 or not 1 <= args.fibonacci <= 40:
        parser.error('positive sample/iteration counts and fibonacci 1..40 are required')
    output = args.output.resolve(); output.mkdir(parents=True, exist_ok=False)
    compiler = args.polyc.resolve()
    report = {'schema_version': 1, 'created_at': datetime.datetime.now(datetime.timezone.utc).isoformat(),
              'host': platform.platform(), 'toolchain_sha256': fingerprints(compiler), 'cases': [],
              'configuration': vars(args) | {'polyc': str(compiler), 'output': str(output)},
              'execution_order_seed': 1729}
    for kernel, (source, expected) in sources(args.loop_iterations, args.call_iterations, args.fibonacci).items():
        folder = output / kernel; folder.mkdir()
        src = folder / 'program.cpp'; src.write_text(source)
        for opt in args.opt_levels:
            for allocator in ('stack', 'linear-scan', 'graph-coloring'):
                executable = folder / f'O{opt}-{allocator}'
                command = [str(compiler), '--strict', '--no-package-index', '--no-aux', '--quiet',
                           f'-O{opt}', '--regalloc=' + allocator, '-o', str(executable), str(src)]
                compiled = invoke(command, folder, args.timeout)
                row = {'kernel': kernel, 'opt': opt, 'allocator': allocator, 'expected_stdout': str(expected) + '\n',
                       'source': str(src), 'source_sha256': hashlib.sha256(source.encode()).hexdigest(),
                       'executable': str(executable), 'compile': compiled, 'samples': [], 'status': 'pending'}
                if compiled['returncode'] != 0 or compiled['timeout'] or not executable.is_file(): row['status'] = 'failed'
                else:
                    row['bytes'] = executable.stat().st_size
                    row['executable_sha256'] = hashlib.sha256(executable.read_bytes()).hexdigest()
                report['cases'].append(row)
                save(output, report)
    randomizer = random.Random(report['execution_order_seed'])
    for iteration in range(args.warmups + args.repetitions):
        order = list(range(len(report['cases']))); randomizer.shuffle(order)
        for index in order:
            row = report['cases'][index]
            if row['status'] == 'failed': continue
            result = invoke([row['executable']], output, args.timeout)
            result['warmup'] = iteration < args.warmups
            row['samples'].append(result)
            okay = result['returncode'] == 0 and not result['timeout'] and result['stdout'] == row['expected_stdout'] and result['stderr'] == ''
            if not okay: row['status'] = 'failed'
        print(f'Checked execution round {iteration + 1}/{args.warmups + args.repetitions}', flush=True)
        save(output, report)
    for row in report['cases']:
        if row['status'] == 'failed': continue
        samples = [sample['elapsed_ms'] for sample in row['samples'] if not sample['warmup']]
        row.update(status='passed', median_ms=statistics.median(samples), min_ms=min(samples), max_ms=max(samples),
                   mean_ms=statistics.mean(samples), stdev_ms=statistics.stdev(samples) if len(samples) > 1 else 0)
    report['toolchain_unchanged'] = fingerprints(compiler) == report['toolchain_sha256']
    save(output, report)
    return int(not report['toolchain_unchanged'] or any(row['status'] != 'passed' for row in report['cases']))


if __name__ == '__main__': raise SystemExit(main())
