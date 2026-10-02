#!/usr/bin/env python3
"""Run native scalar kernels against installed language implementations.

Identical kernel text is embedded in both sources; language-standard entry and
I/O adapters differ. Complete stdout values are checked independently and then
against the official implementation. Missing tools are recorded, never passed
off as an independent comparison. Poly has no separate official implementation.
"""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import signal
import statistics
import struct
import subprocess
import sys
import time
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tests/native_programs'))
from differential_corpus import LANGUAGES, CASES, source


def invoke(command, folder, timeout=45, env=None):
    start = time.perf_counter_ns()
    record = {'command': [str(x) for x in command]}
    try:
        process = subprocess.Popen(command, cwd=folder, env=env, stdout=subprocess.PIPE,
                                   stderr=subprocess.PIPE, start_new_session=os.name != 'nt')
        try:
            stdout, stderr = process.communicate(timeout=timeout)
            record.update(returncode=process.returncode, timeout=False)
        except subprocess.TimeoutExpired:
            if os.name != 'nt': os.killpg(process.pid, signal.SIGKILL)
            else: process.kill()
            stdout, stderr = process.communicate()
            record.update(returncode=process.returncode, timeout=True)
        record.update(stdout=stdout.decode('utf-8', 'replace'), stderr=stderr.decode('utf-8', 'replace'))
    except OSError as error:
        record.update(returncode=None, timeout=False, stdout='', stderr=str(error))
    record['elapsed_ms'] = (time.perf_counter_ns() - start) / 1e6
    return record


def locate(name):
    candidates = [shutil.which(name)]
    candidates += [str(Path(directory) / name) for directory in
                   ('/opt/homebrew/bin', '/usr/local/bin', '/usr/local/go/bin',
                    '/usr/local/share/dotnet', str(Path.home() / '.cargo/bin'))]
    return next((candidate for candidate in candidates if candidate and Path(candidate).is_file()), None)


def references(work, timeout, node=None):
    config = {'cpp': ('clang++', ['--version']), 'python': ('python3', ['--version']),
              'rust': ('rustc', ['--version']), 'go': ('go', ['version']),
              'java': ('javac', ['-version']), 'dotnet': ('dotnet', ['--list-sdks']),
              'javascript': ('node', ['--version']), 'ruby': ('ruby', ['--version'])}
    tools = {}
    for lang, (name, flags) in config.items():
        path = str(node.resolve()) if lang == 'javascript' and node else locate(name)
        record = {'path': path, 'available': False}
        if path:
            record['probe'] = invoke([path, *flags], work, timeout)
            record['available'] = record['probe']['returncode'] == 0 and not record['probe']['timeout']
            if lang == 'dotnet' and not record['probe']['stdout'].strip(): record['available'] = False
            if lang == 'java':
                record['runtime'] = locate('java')
                record['available'] &= bool(record['runtime'])
        else: record['reason'] = f'{name} was not found in PATH or common toolchain directories'
        tools[lang] = record
    tools['poly'] = {'available': False, 'reason': 'Poly has no independent official implementation; value oracle only'}
    return tools


def reference_run(language, text, folder, tool, timeout):
    extension = LANGUAGES[language]
    src = folder / ('Program.java' if language == 'java' else 'reference.' + extension)
    src.write_text(text)
    executable = folder / 'reference-program'
    compiler = tool['path']
    env = os.environ.copy()
    commands = []
    if language == 'cpp': commands = [[compiler, '-std=c++17', '-O0', str(src), '-o', str(executable)], [str(executable)]]
    elif language == 'rust': commands = [[compiler, '--crate-name', 'reference', str(src), '-o', str(executable)], [str(executable)]]
    elif language == 'go':
        env.update(GOCACHE=str(folder / 'go-cache'), GOPATH=str(folder / 'go-path'), GO111MODULE='off', GOPROXY='off')
        commands = [[compiler, 'build', '-o', str(executable), str(src)], [str(executable)]]
    elif language == 'java': commands = [[compiler, str(src)], [tool['runtime'], '-cp', str(folder), 'Program']]
    elif language == 'dotnet':
        sdk = tool['probe']['stdout'].strip().splitlines()[-1].split()[0].split('.')[0]
        (folder / 'reference.csproj').write_text(f'<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net{sdk}.0</TargetFramework><EnableDefaultCompileItems>false</EnableDefaultCompileItems></PropertyGroup><ItemGroup><Compile Include="reference.cs"/></ItemGroup></Project>')
        (folder / 'NuGet.Config').write_text('<configuration><packageSources><clear/></packageSources></configuration>')
        env.update(DOTNET_CLI_HOME=str(folder / 'dotnet-home'), DOTNET_SKIP_FIRST_TIME_EXPERIENCE='1',
                   DOTNET_CLI_TELEMETRY_OPTOUT='1', NUGET_PACKAGES=str(folder / 'nuget'))
        commands = [[compiler, 'build', str(folder / 'reference.csproj'), '--nologo', '--verbosity', 'quiet',
                     '--disable-build-servers', '-o', str(folder / 'bin')],
                    [compiler, str(folder / 'bin/reference.dll')]]
    else: commands = [[compiler, str(src)]]
    records = []
    for command in commands:
        result = invoke(command, folder, timeout, env)
        records.append(result)
        if result['returncode'] != 0 or result['timeout']: break
    return {'source': str(src), 'source_sha256': hashlib.sha256(text.encode()).hexdigest(), 'steps': records,
            'ok': len(records) == len(commands) and all(r['returncode'] == 0 and not r['timeout'] for r in records)}


def normalized(text, kind):
    # Require one complete numeric value per line, retaining sign of zero and
    # every bit of binary64. Nothing is reduced modulo a process exit status.
    lines = text.splitlines()
    if kind == 'i64': return [str(int(line, 10)) for line in lines]
    result = []
    for line in lines:
        value = float.fromhex(line) if '0x' in line.lower() else float(line)
        result.append(struct.pack('>d', value).hex())
    return result


def write_report(folder, report):
    (folder / 'results.json').write_text(json.dumps(report, indent=2, ensure_ascii=False) + '\n')
    lines = ['# Native semantic differential evaluation', '',
             'The kernel text is identical; source entry and output adapters differ. stdout is checked in full.',
             'Finite float output uses exact hexadecimal notation; comparison retains binary64 precision and signed zero. NaN payloads are not encoded.', '',
             '| Language | Official implementation |', '| --- | --- |']
    for language, tool in report['references'].items():
        details = tool.get('path') if tool['available'] else tool.get('reason', 'tool exists but its version probe failed')
        lines.append(f'| {language} | {details} |')
    lines += ['', '| Language | Kernel | Opt | Allocator | Native oracle | Independent comparison |', '| --- | --- | --- | --- | --- | --- |']
    for case in report['cases']:
        lines.append(f"| {case['language']} | {case['case']} | O{case['opt']} | {case['allocator']} | {case['status']} | {case['comparison']} |")
    counts = {name: sum(c['status'] == name for c in report['cases']) for name in ('passed', 'failed')}
    lines += ['', f"Native checks: {counts['passed']} passed, {counts['failed']} failed.",
              f"Independent comparisons passed: {sum(c['comparison'] == 'passed' for c in report['cases'])}.",
              'Skipped references are coverage gaps. They do not invalidate native oracle checks and do not count as independent confirmation.',
              'This suite excludes undefined source behavior, full standard libraries, exception runtimes, and arbitrary-precision integers.']
    (folder / 'report.md').write_text('\n'.join(lines) + '\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--polyc', type=Path, default=ROOT / 'build-release/polyc')
    destination = parser.add_mutually_exclusive_group(required=True)
    destination.add_argument('--output', type=Path, help='new retained artifact directory')
    destination.add_argument('--output-root', type=Path, help='create a unique run directory underneath this root')
    parser.add_argument('--languages', nargs='+', choices=list(LANGUAGES), default=list(LANGUAGES))
    parser.add_argument('--cases', nargs='+', choices=CASES, default=list(CASES))
    parser.add_argument('--opt-levels', nargs='+', type=int, choices=range(4), default=[0, 1, 2, 3])
    parser.add_argument('--allocators', nargs='+', choices=('linear', 'graph'), default=['linear', 'graph'])
    parser.add_argument('--timeout', type=float, default=60)
    parser.add_argument('--node', type=Path, help='explicit installed Node.js runtime')
    parser.add_argument('--require-reference', action='store_true', help='fail when a requested non-Poly official implementation is missing')
    args = parser.parse_args()
    if args.output_root:
        args.output_root.mkdir(parents=True, exist_ok=True)
        output = Path(tempfile.mkdtemp(prefix='differential-', dir=args.output_root.resolve()))
    else:
        output = args.output.resolve(); output.mkdir(parents=True, exist_ok=False)
    print(f'Retained differential artifacts: {output}', flush=True)
    compiler = args.polyc.resolve()
    library_hashes = {path.name: hashlib.sha256(path.read_bytes()).hexdigest()
                      for path in compiler.parent.iterdir() if path.is_file() and path.suffix in ('.dylib', '.so', '.dll')}
    report = {'schema_version': 1, 'toolchain_libraries_sha256': library_hashes, 'created_at': datetime.datetime.now(datetime.timezone.utc).isoformat(),
              'host': platform.platform(), 'compiler': str(compiler),
              'compiler_sha256': hashlib.sha256(compiler.read_bytes()).hexdigest(),
              'references': references(output, args.timeout, args.node), 'cases': []}
    for language in args.languages:
        for case_name in args.cases:
            program = source(language, case_name)
            folder = output / language / case_name; folder.mkdir(parents=True)
            (folder / 'shared-kernel.txt').write_text(program['kernel'])
            src = folder / ('native.' + LANGUAGES[language]); src.write_text(program['native'])
            expected = normalized('\n'.join(str(x) for x in program['expected']), program['kind'])
            tool = report['references'][language]
            reference = reference_run(language, program['reference'], folder, tool, args.timeout) if tool['available'] else None
            reference_values = None
            if reference and reference['ok']:
                try: reference_values = normalized(reference['steps'][-1]['stdout'], program['kind'])
                except ValueError: pass
            for opt in args.opt_levels:
                for allocator in args.allocators:
                    executable = folder / f'native-O{opt}-{allocator}'
                    command = [str(compiler), '--strict', '--no-package-index', '--no-aux', '--quiet',
                               f'-O{opt}', '--regalloc=' + allocator, '-o', str(executable), str(src)]
                    compiled = invoke(command, folder, args.timeout)
                    row = {'language': language, 'case': case_name, 'opt': opt, 'allocator': allocator,
                           'source': str(src), 'kernel_sha256': hashlib.sha256(program['kernel'].encode()).hexdigest(),
                           'compile': compiled, 'expected_values': expected, 'kind': program['kind'],
                           'reference': reference, 'status': 'failed', 'comparison': 'skipped'}
                    if compiled['returncode'] == 0 and not compiled['timeout'] and executable.is_file():
                        run = invoke([str(executable)], folder, args.timeout); row['run'] = run
                        try: row['actual_values'] = normalized(run['stdout'], program['kind'])
                        except ValueError: row['actual_values'] = None
                        if run['returncode'] == program['native_exit'] and not run['timeout'] and not run['stderr'] and row['actual_values'] == expected:
                            row['status'] = 'passed'
                        row['executable_bytes'] = executable.stat().st_size
                        row['executable_sha256'] = hashlib.sha256(executable.read_bytes()).hexdigest()
                        if reference:
                            row['reference_values'] = reference_values
                            row['comparison'] = 'passed' if reference_values == expected == row['actual_values'] else 'failed'
                            if row['comparison'] == 'failed': row['status'] = 'failed'
                    elif reference: row['comparison'] = 'blocked by native compile failure'
                    report['cases'].append(row)
                    write_report(output, report)
                    print(f"{language:10} {case_name:18} O{opt} {allocator:6} {row['status']:6} reference={row['comparison']}", flush=True)
    missing = args.require_reference and any(not report['references'][lang]['available'] for lang in args.languages if lang != 'poly')
    return int(missing or any(row['status'] != 'passed' for row in report['cases']))


if __name__ == '__main__': raise SystemExit(main())
