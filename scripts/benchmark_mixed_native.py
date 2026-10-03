#!/usr/bin/env python3
"""Measure the real mixed order application after complete output validation.

Use a quiet host and fresh output directory. Application copies and official
reference checks happen outside timed native compilation. OS caches stay warm.
"""
import argparse
import datetime
import json
import math
import os
from pathlib import Path
import platform
import random
import statistics
import sys
sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tests/native_programs'))
from mixed_support import (copy_application, digest, invoke, native_artifact, official_references,
                           run_scenarios, scaled_scenarios, scenarios, succeeded, validate_build_report)


def distribution(values):
    values = sorted(values)
    return {'n': len(values), 'min': values[0], 'median': statistics.median(values),
            'mean': statistics.mean(values), 'max': values[-1],
            'p95': values[math.ceil(.95 * len(values)) - 1],
            'stddev': statistics.stdev(values) if len(values) > 1 else 0}


def write_report(output, report):
    (output / 'results.json').write_text(json.dumps(report, indent=2, ensure_ascii=False) + '\n')
    fmt = lambda value: '—' if value is None else f'{value:.3f}'
    lines = ['# Mixed native compile and run benchmark', '',
             f"Host: `{report['host']}`; compiler SHA-256: `{report['compiler_sha256']}`.",
             f"Protocol: {report['configuration']['repetitions']} measured repetitions and {report['configuration']['warmups']} warmups per configuration.",
             f"Official source comparison: **{report['reference']['status']}**.", '',
             f"Overall result: **{'pending' if 'passed' not in report else 'passed' if report['passed'] else 'failed'}**; toolchain unchanged: `{report.get('toolchain_unchanged', 'pending')}`.", '',
             '| Opt | Allocator | Status | Compile median ms | First launch median ms | Compile peak RSS median MiB | Binary bytes |',
             '| --- | --- | --- | ---: | ---: | ---: | ---: |']
    for row in report['configurations']:
        rss = row.get('compile_peak_rss_bytes', {}).get('median')
        lines.append(f"| O{row['opt']} | {row['allocator']} | {row['status']} | {fmt(row.get('compile_ms', {}).get('median'))} | {fmt(row.get('first_launch_ms', {}).get('median'))} | {fmt(None if rss is None else rss / 1048576)} | {row.get('executable_bytes', '—')} |")
    lines += ['', '## Runtime by input size (previously launched executable)', '',
              '| Opt | Allocator | Rows | Median ms | p95 ms | Peak RSS median MiB | Output bytes |',
              '| --- | --- | ---: | ---: | ---: | ---: | ---: |']
    for row in report['configurations']:
        for scale in row.get('runtime_by_rows', []):
            rss = scale.get('peak_rss_bytes', {}).get('median')
            lines.append(f"| O{row['opt']} | {row['allocator']} | {scale['row_count']} | {fmt(scale['elapsed_ms']['median'])} | {fmt(scale['elapsed_ms']['p95'])} | {fmt(None if rss is None else rss / 1048576)} | {scale['output_bytes']} |")
    lines += ['', '## Per-module compilation', '', '| Opt | Allocator | Language | Source | Median ms |', '| --- | --- | --- | --- | ---: |']
    for row in report['configurations']:
        for module in row.get('module_ms', []):
            lines.append(f"| O{row['opt']} | {row['allocator']} | {module['language']} | {module['source']} | {fmt(module['distribution']['median'])} |")
    lines += ['', '## Measurement boundaries', '',
              '- Full application: Poly orchestration plus C++, Python, Rust and Go source/package modules.',
              '- Each timed build gets a fresh source/output directory; source copying and reference compilation are excluded. OS file caches are not flushed.',
              '- Compilation includes driver/frontend/backend/linking subprocess work. Per-module timers come from the compiler build report; their sum excludes some orchestration/link/runtime costs.',
              '- Runtime includes process startup, CSV input, complete stdout and result-file writes. It is not compute-only throughput.',
              '- Runtime call tracing is disabled during these performance measurements.',
              '- Input sizes repeat the baseline rule mix with unique order IDs. Every decision and summary is independently recomputed; complete stdout and file contents must match at every size.',
              '- First-launch latency is retained separately from the first acceptance run. After all ten acceptance scenarios, each size runs independently on the same executable; no acceptance timing is reused. Scale order is shuffled using the recorded seed.',
              '- Scale measurements use a previously launched executable but a fresh process each time, including startup and all I/O. Warmup builds also exercise every input size.',
              '- Official source comparison covers the four small successful acceptance scenarios. Larger inputs are checked against the independent integer oracle; this does not add new business-rule diversity.',
              '- Before accepting a sample, the same executable must pass baseline, changed-input, extended-input, reordered-input and all negative-input cases.',
              '- Peak RSS is the OS wait4 per-child high-water measurement, not the sum of simultaneous process-tree RSS. Missing memory metrics remain null.',
              '- Warmups are retained but excluded from aggregates. Any failed sample invalidates its entire configuration and removes its timing aggregates.',
              '- Configuration order is shuffled within each round using the recorded seed. Compiler, linker, shared-library and source hashes are retained.',
              '- This local benchmark does not imply remote CI execution or cross-machine performance.']
    (output / 'report.md').write_text('\n'.join(lines) + '\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--polyc', type=Path, default=ROOT / 'build-release/polyc')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--opt-levels', nargs='+', type=int, choices=range(4), default=[0, 1, 2, 3])
    parser.add_argument('--allocators', nargs='+', choices=('linear', 'graph'), default=['linear', 'graph'])
    parser.add_argument('--repetitions', type=int, default=7)
    parser.add_argument('--warmups', type=int, default=1)
    parser.add_argument('--row-counts', nargs='+', type=int, default=[8, 800, 8000])
    parser.add_argument('--timeout', type=float, default=90)
    parser.add_argument('--seed', type=int, default=20261003)
    parser.add_argument('--require-reference', action='store_true')
    args = parser.parse_args()
    if platform.system() not in ('Darwin', 'Linux') or platform.machine().lower() not in ('x86_64', 'amd64', 'arm64', 'aarch64'):
        parser.error('this native gate requires a Linux/macOS x86_64 or ARM64 host; unsupported hosts are not skipped')
    if args.repetitions < 1 or args.warmups < 0 or args.timeout <= 0: parser.error('invalid repetition/warmup/timeout values')
    if any(count < 1 for count in args.row_counts) or len(set(args.row_counts)) != len(args.row_counts):
        parser.error('row counts must be distinct positive integers')
    compiler, output = args.polyc.resolve(), args.output.resolve()
    if not compiler.is_file(): parser.error('compiler does not exist')
    output.mkdir(parents=True, exist_ok=False)
    app = output / 'source'; hashes = copy_application(app); cases = scenarios(app)
    scale_cases = scaled_scenarios(cases[0]['rows'], args.row_counts)
    libraries = {str(path): digest(path) for path in sorted(compiler.parent.iterdir())
                 if path.is_file() and (path.suffix in ('.so', '.dylib', '.dll') or path.name in ('polyc', 'polyld'))}
    report = {'schema': 'polyglot.mixed-benchmark.v1', 'created_at': datetime.datetime.now(datetime.timezone.utc).isoformat(),
              'host': platform.platform(), 'machine': platform.machine(), 'compiler': str(compiler),
              'logical_cpus': os.cpu_count(), 'python_version': platform.python_version(),
              'script_sha256': {str(path.relative_to(ROOT)): digest(path) for path in
                                (Path(__file__).resolve(), ROOT / 'tests/native_programs/mixed_support.py')},
              'compiler_sha256': digest(compiler), 'toolchain_sha256': libraries, 'source_sha256': hashes,
              'configuration': vars(args) | {'polyc': str(compiler), 'output': str(output)},
              'runtime_protocol': 'independent size runs after ten acceptance scenarios; seeded shuffled size order; first launch reported separately',
              'runtime_inputs': [{key: case[key] for key in ('row_count', 'input_sha256')}
                                 | {'output_bytes': len(case['expected_stdout'].encode())} for case in scale_cases],
              'reference': official_references(app, output / 'references', cases, args.timeout),
              'schedule': [], 'configurations': []}
    configurations = {(opt, alloc): {'opt': opt, 'allocator': alloc, 'status': 'pending', 'samples': []}
                      for opt in args.opt_levels for alloc in args.allocators}
    rng = random.Random(args.seed)
    scale_rng = random.Random(args.seed ^ 0x5343414C45)
    for repetition in range(args.warmups + args.repetitions):
        order = list(configurations); rng.shuffle(order)
        for opt, allocator in order:
            row = configurations[(opt, allocator)]
            folder = output / f'O{opt}-{allocator}' / str(repetition); folder.mkdir(parents=True)
            copy_application(folder / 'application', app)
            executable = folder / 'order-risk'; build_report = folder / 'build-report.json'
            compiled = invoke([compiler, '--strict', '--no-package-index', '--quiet', f'-O{opt}',
                '--regalloc=' + allocator, '--build-report=' + str(build_report), '-o', executable,
                folder / 'application/order_risk.poly'], folder, args.timeout, memory=True)
            sample = {'warmup': repetition < args.warmups, 'repetition': repetition,
                      'compile': compiled, 'passed': False}
            report['schedule'].append({'repetition': repetition, 'opt': opt, 'allocator': allocator})
            if succeeded(compiled) and native_artifact(executable):
                sample['build_report'], sample['build_errors'] = validate_build_report(build_report, opt, allocator)
                sample['scenarios'] = run_scenarios(executable, folder / 'runs', cases, args.timeout, memory=True)
                sample['scale_runs'] = []
                scale_order = list(scale_cases); scale_rng.shuffle(scale_order)
                sample['scale_order'] = [case['row_count'] for case in scale_order]
                for case in scale_order:
                    run = run_scenarios(executable, folder / 'scales', [case], args.timeout, memory=True)[0]
                    run.update(row_count=case['row_count'], input_sha256=case['input_sha256'],
                               output_bytes=len(case['expected_stdout'].encode()))
                    sample['scale_runs'].append(run)
                sample['passed'] = (not sample['build_errors'] and all(case['passed'] for case in sample['scenarios'])
                                    and all(case['passed'] for case in sample['scale_runs']))
                sample['executable_bytes'] = executable.stat().st_size
                sample['executable_sha256'] = digest(executable)
            row['samples'].append(sample)
            row['status'] = 'passed' if all(s['passed'] for s in row['samples']) else 'failed'
            report['configurations'] = list(configurations.values())
            write_report(output, report)
            print(f"round {repetition} O{opt} {allocator}: {'PASS' if sample['passed'] else 'FAIL'}", flush=True)
    report['toolchain_unchanged'] = all(Path(path).is_file() and digest(path) == sha for path, sha in libraries.items())
    for row in configurations.values():
        if row['status'] != 'passed' or not report['toolchain_unchanged']: continue
        measured = [sample for sample in row['samples'] if not sample['warmup']]
        row['compile_ms'] = distribution([sample['compile']['elapsed_ms'] for sample in measured])
        row['first_launch_ms'] = distribution([sample['scenarios'][0]['run']['elapsed_ms'] for sample in measured])
        rss = [sample['compile']['peak_rss_bytes'] for sample in measured]
        if all(value is not None for value in rss): row['compile_peak_rss_bytes'] = distribution(rss)
        row['runtime_by_rows'] = []
        for count in args.row_counts:
            runs = [next(run for run in sample['scale_runs'] if run['row_count'] == count) for sample in measured]
            scale = {'row_count': count, 'elapsed_ms': distribution([run['run']['elapsed_ms'] for run in runs]),
                     'output_bytes': runs[0]['output_bytes'], 'input_sha256': runs[0]['input_sha256']}
            rss = [run['run']['peak_rss_bytes'] for run in runs]
            if all(value is not None for value in rss): scale['peak_rss_bytes'] = distribution(rss)
            row['runtime_by_rows'].append(scale)
            if count == 8:
                row['run_ms'] = scale['elapsed_ms']
                if 'peak_rss_bytes' in scale: row['run_peak_rss_bytes'] = scale['peak_rss_bytes']
        row['executable_bytes'] = measured[-1]['executable_bytes']
        module_values = {}
        for sample in measured:
            for module in sample['build_report']['modules']:
                # Relative source suffix removes the fresh snapshot directory.
                path = str(module['source']).split('/application/')[-1]
                module_values.setdefault((module['language'], path), []).append(module['elapsed_ms'])
        row['module_ms'] = [{'language': key[0], 'source': key[1], 'distribution': distribution(values)}
                            for key, values in sorted(module_values.items()) if len(values) == len(measured)]
    report['finished_at'] = datetime.datetime.now(datetime.timezone.utc).isoformat()
    report['passed'] = (bool(configurations) and report['toolchain_unchanged']
        and all(row['status'] == 'passed' for row in configurations.values())
        and report['reference']['status'] != 'failed'
        and (not args.require_reference or report['reference']['status'] == 'passed'))
    write_report(output, report)
    print(f"Retained benchmark: {output}; passed={report['passed']}", flush=True)
    return 0 if report['passed'] else 1


if __name__ == '__main__': raise SystemExit(main())
