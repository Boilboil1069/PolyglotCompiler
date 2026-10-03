#!/usr/bin/env python3
"""Require a real Qt workspace, source documentation and definition navigation."""
import argparse
import json
import os
from pathlib import Path
import platform
import struct
import sys
import tempfile
sys.dont_write_bytecode = True
from mixed_support import ROOT, copy_application, invoke, protocol, read_rows, succeeded


def png(path):
    if not path.is_file(): return False
    data = path.read_bytes()
    return len(data) > 1000 and data[:8] == b'\x89PNG\r\n\x1a\n' and all(n > 100 for n in struct.unpack('>II', data[16:24]))


def qprocess_arguments(arguments):
    """Quote for QProcess::splitCommand, whose literal quote is three quotes."""
    return ' '.join('"' + str(argument).replace('"', '"""') + '"' for argument in arguments)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--polyui', type=Path, required=True)
    destination = parser.add_mutually_exclusive_group(required=True)
    destination.add_argument('--output', type=Path)
    destination.add_argument('--output-root', type=Path)
    parser.add_argument('--timeout', type=float, default=90)
    parser.add_argument('--include-native-trace', action='store_true',
                        help='also compile/run the real order application and verify traced UI values (ARM64 only)')
    args = parser.parse_args()
    if args.include_native_trace and (platform.system() not in ('Darwin', 'Linux') or
                                     platform.machine().lower() not in ('arm64', 'aarch64')):
        parser.error('the native trace gate requires Linux/macOS ARM64; unsupported targets are not passed')
    executable = args.polyui.resolve()
    if not executable.is_file(): parser.error('Qt executable is required; skipping is not supported')
    if args.output_root:
        args.output_root.mkdir(parents=True, exist_ok=True)
        output = Path(tempfile.mkdtemp(prefix='qt-smoke-', dir=args.output_root.resolve()))
    else:
        output = args.output.resolve(); output.mkdir(parents=True, exist_ok=False)
    app = ROOT / 'examples/editor_cross_language_demo'
    cases = []
    for language, symbol, filename, line, column in [
            ('cpp', 'subtotal', 'pricing.cpp', 7, 5),
            ('python', 'cents_to_dollars', 'reporting.py', 1, 5)]:
        screenshot = output / (language + '.png')
        env = os.environ.copy(); env['QT_QPA_PLATFORM'] = 'offscreen'
        run = invoke([executable, '--headless', '--ui-smoke', '--folder', app,
                      '--file', app / 'order_flow.poly', '--view', 'split', '--peek', symbol,
                      '--screenshot', screenshot], ROOT, args.timeout, env=env)
        evidence = None
        for text in (run['stdout'] + '\n' + run['stderr']).splitlines():
            try: value = json.loads(text)
            except ValueError: continue
            if isinstance(value, dict) and 'inlineDocumentation' in value: evidence = value
        passed = (succeeded(run) and evidence is not None
                  and all(evidence.get(field) is True for field in ('inlineDocumentation', 'sourcePreview', 'contextMenuDefinition'))
                  and Path(evidence.get('target', '')).name == filename
                  and evidence.get('line') == line and evidence.get('column') == column
                  and png(screenshot) and png(Path(str(screenshot) + '.context.png')))
        cases.append({'language': language, 'passed': passed, 'evidence': evidence, 'run': run})
    if args.include_native_trace:
        work = output / 'native-trace'; work.mkdir()
        app = work / 'application'; copy_application(app)
        expected = protocol(read_rows(app / 'data/orders.csv'))
        result_file = work / 'order-results.txt'
        screenshot = work / 'runtime.png'
        env = os.environ.copy(); env['QT_QPA_PLATFORM'] = 'offscreen'
        run = invoke([executable, '--headless', '--ui-trace-smoke', '--folder', app,
                      '--file', app / 'order_risk.poly', '--run-args',
                      qprocess_arguments(['data/orders.csv', result_file]),
                      '--screenshot', screenshot], ROOT, args.timeout, env=env)
        evidence = None
        for text in (run['stdout'] + '\n' + run['stderr']).splitlines():
            try: value = json.loads(text)
            except ValueError: continue
            if isinstance(value, dict) and 'realRuntimeSamples' in value: evidence = value
        file_contents = result_file.read_text() if result_file.is_file() else None
        passed = (succeeded(run) and evidence is not None
                  and all(evidence.get(field) is True for field in ('realRuntimeSamples', 'compiledAndRan',
                          'sourceHighlight', 'portValues', 'overlayOffRestoresStatic'))
                  and evidence.get('exitCode') == 0 and evidence.get('sampleCount') == 128
                  and evidence.get('mappedCallSites') == 16 and png(screenshot) and file_contents == expected)
        cases.append({'language': 'mixed-runtime-trace', 'passed': passed, 'evidence': evidence,
                      'expected_file': expected, 'file_contents': file_contents, 'run': run})
    report = {'schema': 'polyglot.qt-workspace-gate.v1', 'passed': all(case['passed'] for case in cases), 'cases': cases}
    (output / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({'passed': report['passed'], 'output': str(output)}))
    return 0 if report['passed'] else 1


if __name__ == '__main__': raise SystemExit(main())
