#!/usr/bin/env python3
"""Compile AND execute all native artifacts; save raw measurements and failures.

No third-party Python modules are required. Each measured compile uses a fresh
output path, --strict, and --no-package-index. The existing OS page cache is not
flushed. Run latency includes process launch and is not kernel-only throughput.
"""
import argparse
import datetime
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path
import platform
import statistics
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('corpus', ROOT / 'tests/native_programs/corpus.py')
corpus = importlib.util.module_from_spec(spec)
spec.loader.exec_module(corpus)


def invoke(command, cwd, timeout):
    start = time.perf_counter_ns()
    try:
        result = subprocess.run(command, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                timeout=timeout)
        return {'command': [str(x) for x in command], 'elapsed_ms': (time.perf_counter_ns() - start) / 1e6,
                'returncode': result.returncode, 'stdout': result.stdout.decode('utf-8', 'replace'),
                'stderr': result.stderr.decode('utf-8', 'replace'), 'timeout': False}
    except (subprocess.TimeoutExpired, OSError) as error:
        return {'command': [str(x) for x in command], 'elapsed_ms': (time.perf_counter_ns() - start) / 1e6,
                'returncode': None, 'stdout': '', 'stderr': str(error), 'timeout': isinstance(error, subprocess.TimeoutExpired)}


def distribution(values):
    values = sorted(values)
    return {'n': len(values), 'min_ms': min(values), 'median_ms': statistics.median(values),
            'mean_ms': statistics.mean(values), 'p95_ms': values[math.ceil(.95 * len(values)) - 1],
            'max_ms': max(values), 'stddev_ms': statistics.stdev(values) if len(values) > 1 else 0}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def native_magic(path):
    header = path.read_bytes()[:4]
    return header in (b'\xcf\xfa\xed\xfe', b'\xfe\xed\xfa\xcf', b'\x7fELF') or header[:2] == b'MZ'


def write_report(output, report):
    (output / 'results.json').write_text(json.dumps(report, indent=2, ensure_ascii=False) + '\n')
    lines = ['# Native compilation and execution benchmark', '',
             f"Host: `{report['host']}`. Compiler SHA-256: `{report['compiler_sha256']}`.", '',
             'Times include compiler/linker processes and OS process startup. Warm OS caches; no incremental artifact reuse.', '',
             '| Language | Workload | Opt | Result | Compile median ms | Run median ms | Executable bytes |',
             '| --- | --- | --- | --- | ---: | ---: | ---: |']
    for case in report['cases']:
        c = case.get('compile', {}).get('median_ms')
        r = case.get('run', {}).get('median_ms')
        lines.append(f"| {case['language']} | {case['workload']} | O{case['opt']} | {case['status']} | {c if c is not None else '—'} | {r if r is not None else '—'} | {case.get('executable_bytes', '—')} |")
    lines += ['', f"Passed: {sum(c['status'] == 'passed' for c in report['cases'])}/{len(report['cases'])} configurations.",
              'See results.json and the retained source, binaries, and logs for each configuration. Failed configurations have no performance aggregate.']
    (output / 'report.md').write_text('\n'.join(lines) + '\n')


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--polyc', type=Path, default=ROOT / 'build-release/polyc')
    ap.add_argument('--output', type=Path, required=True, help='new directory; existing results are never overwritten')
    ap.add_argument('--languages', nargs='+', choices=list(corpus.LANGUAGES), default=list(corpus.LANGUAGES))
    ap.add_argument('--opt-levels', nargs='+', type=int, choices=range(4), default=[0, 1, 2, 3])
    ap.add_argument('--scales', nargs='*', type=int, default=[8, 64, 256])
    ap.add_argument('--repetitions', type=int, default=7)
    ap.add_argument('--warmups', type=int, default=1)
    ap.add_argument('--timeout', type=float, default=30)
    ap.add_argument('--target', help='optional target triple; execution must be supported on this host')
    args = ap.parse_args()
    if args.repetitions < 1 or args.warmups < 0 or args.timeout <= 0 or any(n < 1 for n in args.scales):
        ap.error('positive repetitions, scales and timeout, and nonnegative warmups are required')
    compiler = args.polyc.resolve()
    if not compiler.is_file(): ap.error(f'compiler does not exist: {compiler}')
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    report = {'schema_version': 1, 'created_at': datetime.datetime.now(datetime.timezone.utc).isoformat(),
              'host': platform.platform(), 'machine': platform.machine(), 'python': sys.version,
              'compiler': str(compiler), 'compiler_sha256': digest(compiler),
              'configuration': vars(args) | {'polyc': str(compiler), 'output': str(output)}, 'cases': []}
    linker = compiler.with_name('polyld.exe' if os.name == 'nt' else 'polyld')
    if linker.exists(): report['linker_sha256'] = digest(linker)
    report['toolchain_libraries'] = {p.name: digest(p) for p in sorted(compiler.parent.iterdir())
                                     if p.is_file() and p.suffix in ('.dylib', '.so', '.dll')}
    report['compiler_version'] = invoke([str(compiler), '--version'], output, args.timeout)
    workloads = ['calls', 'control_flow', 'recursion'] + [f'scale_{n}' for n in args.scales]
    for language in args.languages:
        for workload in workloads:
            source, expected = corpus.program(language, workload)
            for opt in args.opt_levels:
                folder = output / language / workload / f'O{opt}'
                folder.mkdir(parents=True)
                src = folder / ('program.' + corpus.LANGUAGES[language]); src.write_text(source)
                case = {'language': language, 'workload': workload, 'opt': opt, 'expected_exit': expected,
                        'source': str(src), 'source_sha256': digest(src), 'source_bytes': src.stat().st_size,
                        'status': 'passed', 'samples': []}
                for repetition in range(args.warmups + args.repetitions):
                    sample_dir = folder / str(repetition); sample_dir.mkdir()
                    exe = sample_dir / ('program.exe' if os.name == 'nt' else 'program')
                    command = [str(compiler), '--strict', '--no-package-index', '--no-aux', '--quiet',
                               '--progress=json', f'-O{opt}', '-o', str(exe), str(src)]
                    if args.target: command.append('--target=' + args.target)
                    compiled = invoke(command, sample_dir, args.timeout)
                    (sample_dir / 'compile.log').write_text(compiled['stdout'] + compiled['stderr'])
                    stages = {}
                    for line in compiled['stdout'].splitlines():
                        try: event = json.loads(line)
                        except json.JSONDecodeError: continue
                        if event.get('event') == 'stage_end' and not event.get('stage', '').startswith('Staged '):
                            stages[event['stage']] = event['elapsed_ms']
                    sample = {'warmup': repetition < args.warmups, 'compile': compiled, 'stages_ms': stages}
                    case['samples'].append(sample)
                    valid = compiled['returncode'] == 0 and exe.is_file() and native_magic(exe)
                    if valid:
                        run = invoke([str(exe)], sample_dir, args.timeout)
                        sample['run'] = run
                        valid = run['returncode'] == expected and run['stdout'] == '' and run['stderr'] == ''
                    if not valid:
                        case['status'] = 'failed'
                        break
                    sample['executable_bytes'] = exe.stat().st_size
                    sample['executable_sha256'] = digest(exe)
                if case['status'] == 'passed':
                    measured = [s for s in case['samples'] if not s['warmup']]
                    case['compile'] = distribution([s['compile']['elapsed_ms'] for s in measured])
                    case['run'] = distribution([s['run']['elapsed_ms'] for s in measured])
                    common_stages = set.intersection(*(set(s['stages_ms']) for s in measured))
                    case['stages'] = {stage: distribution([s['stages_ms'][stage] for s in measured]) for stage in sorted(common_stages)}
                    case['executable_bytes'] = measured[-1]['executable_bytes']
                    case['source_bytes_per_second'] = case['source_bytes'] * 1000 / case['compile']['median_ms']
                report['cases'].append(case)
                write_report(output, report)
                print(f"{language:10} {workload:14} O{opt} {case['status']}", flush=True)
    return 1 if any(c['status'] != 'passed' for c in report['cases']) else 0


if __name__ == '__main__':
    raise SystemExit(main())
