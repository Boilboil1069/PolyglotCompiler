#!/usr/bin/env python3
"""Full-output acceptance for one Poly project with C++, Python, Rust and Go."""
import argparse
import datetime
import json
import os
from pathlib import Path
import platform
import sys
import tempfile
sys.dont_write_bytecode = True
from mixed_support import (ROOT, copy_application, digest, invoke, native_artifact, official_references,
                           run_scenarios, scenarios, succeeded, validate_build_report)


def write_report(output, report):
    (output / 'results.json').write_text(json.dumps(report, indent=2, ensure_ascii=False) + '\n')
    lines = ['# Mixed native application regression', '',
             f"Host: `{report['host']}`. Compiler: `{report['compiler']}`.",
             f"Compiler SHA-256: `{report['compiler_sha256']}`.",
             f"Official four-language comparison: **{report['reference']['status']}**.", '',
             '| Opt | Allocator | Compile and module inheritance | Scenarios passed | Result |',
             '| --- | --- | --- | ---: | --- |']
    for row in report['configurations']:
        count = sum(case['passed'] for case in row.get('scenarios', []))
        lines.append(f"| O{row['opt']} | {row['allocator']} | {'passed' if row.get('build_verified') else 'failed'} | {count}/{len(row.get('scenarios', []))} | {row['status']} |")
    lines += ['', 'Each success requires exact stdout, exact result-file contents, empty stderr and exit 0.',
              'Negative fixtures require the documented nonzero exit, expected partial output, and no success summary.',
              'The same executable is reused across baseline, changed, extended, reordered and invalid inputs.',
              'Official tools compile the original foreign module/package bodies; a Python adapter composes their results. Poly has no independent official implementation.',
              'Missing official tools are coverage gaps; --require-reference makes any gap a failing gate.',
              'This is acceptance evidence. These single-run timings are not a performance benchmark.']
    if report.get('error'): lines += ['', 'Error: ' + report['error']]
    (output / 'report.md').write_text('\n'.join(lines) + '\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--polyc', type=Path, default=ROOT / 'build-release/polyc')
    destination = parser.add_mutually_exclusive_group(required=True)
    destination.add_argument('--output', type=Path)
    destination.add_argument('--output-root', type=Path)
    parser.add_argument('--opt-levels', nargs='+', type=int, choices=range(4), default=[0, 1, 2, 3])
    parser.add_argument('--allocators', nargs='+', choices=('linear', 'graph'), default=['linear', 'graph'])
    parser.add_argument('--timeout', type=float, default=90)
    parser.add_argument('--require-reference', action='store_true')
    args = parser.parse_args()
    if platform.system() not in ('Darwin', 'Linux') or platform.machine().lower() not in ('x86_64', 'amd64', 'arm64', 'aarch64'):
        parser.error('this native gate requires a Linux/macOS x86_64 or ARM64 host; unsupported hosts are not skipped')
    if args.timeout <= 0: parser.error('timeout must be positive')
    compiler = args.polyc.resolve()
    if not compiler.is_file(): parser.error('compiler does not exist: ' + str(compiler))
    if args.output_root:
        args.output_root.mkdir(parents=True, exist_ok=True)
        output = Path(tempfile.mkdtemp(prefix='mixed-', dir=args.output_root.resolve()))
    else:
        output = args.output.resolve(); output.mkdir(parents=True, exist_ok=False)
    snapshot = output / 'application'
    hashes = copy_application(snapshot)
    cases = scenarios(snapshot)
    toolchain = {str(p): digest(p) for p in sorted(compiler.parent.iterdir()) if p.is_file()
                 and (p.suffix in ('.so', '.dylib', '.dll') or p.name in ('polyc', 'polyld'))}
    report = {'schema': 'polyglot.mixed-regression.v1', 'created_at': datetime.datetime.now(datetime.timezone.utc).isoformat(),
              'host': platform.platform(), 'compiler': str(compiler), 'compiler_sha256': digest(compiler),
              'source_sha256': hashes, 'toolchain_sha256': toolchain, 'required_reference': args.require_reference,
              'matrix': {'opt_levels': args.opt_levels, 'allocators': args.allocators}, 'configurations': [],
              'reference': official_references(snapshot, output / 'references', cases, args.timeout)}
    for opt in args.opt_levels:
        for allocator in args.allocators:
            work = output / f'O{opt}-{allocator}'; work.mkdir()
            app = work / 'application'; copy_application(app, snapshot)
            executable = work / ('order-risk.exe' if os.name == 'nt' else 'order-risk')
            build_report = work / 'build-report.json'
            compiled = invoke([compiler, '--strict', '--no-package-index', '--quiet',
                               f'-O{opt}', '--regalloc=' + allocator, '--build-report=' + str(build_report),
                               '-o', executable, app / 'order_risk.poly'], work, args.timeout)
            row = {'opt': opt, 'allocator': allocator, 'compile': compiled, 'status': 'failed', 'build_verified': False}
            if succeeded(compiled) and native_artifact(executable):
                row['build_report'], row['build_errors'] = validate_build_report(build_report, opt, allocator)
                row['build_verified'] = not row['build_errors']
                row['executable_sha256'] = digest(executable)
                row['executable_bytes'] = executable.stat().st_size
                row['scenarios'] = run_scenarios(executable, work / 'runs', cases, args.timeout)
                if row['build_verified'] and all(case['passed'] for case in row['scenarios']): row['status'] = 'passed'
            report['configurations'].append(row)
            write_report(output, report)
            print(f"mixed O{opt} {allocator}: {row['status']}", flush=True)
    report['compiler_unchanged'] = digest(compiler) == report['compiler_sha256']
    report['toolchain_unchanged'] = all(Path(p).is_file() and digest(p) == sha for p, sha in toolchain.items())
    report['passed'] = (bool(report['configurations']) and report['compiler_unchanged'] and report['toolchain_unchanged']
        and all(row['status'] == 'passed' for row in report['configurations'])
        and report['reference']['status'] != 'failed'
        and (not args.require_reference or report['reference']['status'] == 'passed'))
    write_report(output, report)
    print(f"Artifacts: {output}; official comparison: {report['reference']['status']}", flush=True)
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
