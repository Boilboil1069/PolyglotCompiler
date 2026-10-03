#!/usr/bin/env python3
"""Execute instrumented ARM64 programs and validate their actual CALL records."""
import argparse
import json
from pathlib import Path
import struct
import sys
import tempfile

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from differential_native import invoke

CPP = '''int twice(int x) { return x * 2; }
double scaled(double x) { return x * 1.5; }
float narrow(float x) { return x; }
'''
PYTHON = '''def wide(x: int) -> int:
    return x
'''
SOURCE = '''IMPORT cpp::math;
IMPORT python::numbers;
FUNC main() -> INT {
    VAR n = 0;
    WHILE n < 3 {
        LET value = CALL(cpp, math::twice, n - 1);
        print_i64(value);
        n = n + 1;
    }
    LET a = CALL(cpp, math::scaled, -21);
    print_f64(a);
    LET b = CALL(cpp, math::narrow, 1.25);
    print_f64(b);
    LET c = CALL(python, numbers::wide, 9007199254740993);
    print_i64(c);
    LET d = CALL(cpp, math::twice, 4); LET e = CALL(cpp, math::twice, 5);
    print_i64(d + e);
    RETURN 0;
}
'''
EXPECTED_STDOUT = '-2\n0\n2\n-0x1.f800000000000p+4\n0x1.4000000000000p+0\n9007199254740993\n18\n'


def bits(value, floating=False, width=64):
    if floating:
        return int.from_bytes(struct.pack('<f' if width == 32 else '<d', value), 'little')
    return value & ((1 << width) - 1)


def validate(trace, source):
    metadata = json.loads(Path(str(trace) + '.sites.json').read_text())
    assert metadata['schema'] == 'polyglot.native-call-sites.v1'
    assert metadata['scope'] == 'foreign-call-boundaries'
    assert metadata['threading'] == 'single-thread'
    sites = {s['id']: s for s in metadata['sites']}
    events = [json.loads(line) for line in trace.read_text().splitlines()]
    assert len(sites) == 6 and len(events) == 8
    assert len({s['source']['column'] for s in sites.values() if s['source']['line'] == 16}) == 2
    expected = [('math::twice', 'i32', [-1], -2), ('math::twice', 'i32', [0], 0),
                ('math::twice', 'i32', [1], 2), ('math::scaled', 'f64', [-21.0], -31.5),
                ('math::narrow', 'f32', [1.25], 1.25),
                ('numbers::wide', 'i64', [9007199254740993], 9007199254740993),
                ('math::twice', 'i32', [4], 8), ('math::twice', 'i32', [5], 10)]
    for number, (event, (callee, typ, args, result)) in enumerate(zip(events, expected), 1):
        assert event['schema'] == 'polyglot.native-calltrace.v1'
        assert all(isinstance(event[field], str) for field in (
            'site_id', 'instance_id', 'parent_instance_id', 'started_ticks',
            'ended_ticks', 'ticks_per_second', 'result_bits'))
        assert event['instance_id'] == str(number) and event['parent_instance_id'] == '0'
        assert int(event['ticks_per_second']) > 0
        assert int(event['ended_ticks']) >= int(event['started_ticks']) > 0
        site = sites[event['site_id']]
        assert Path(site['source']['file']).resolve() == source.resolve()
        assert site['source']['column'] > 0 and site['source']['line'] > 0
        assert site['callee'] == callee and site['result_type'] == typ
        assert [p['type'] for p in site['arguments']] == [typ]
        width = int(typ[1:]); mask = (1 << width) - 1
        assert [int(x) & mask for x in event['argument_bits']] == [bits(x, typ[0] == 'f', width) for x in args]
        assert int(event['result_bits']) & mask == bits(result, typ[0] == 'f', width)
    return len(events)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--polyc', type=Path, default=ROOT / 'build-release/polyc')
    parser.add_argument('--output-root', type=Path, required=True)
    args = parser.parse_args()
    args.output_root.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='trace-', dir=args.output_root.resolve()))
    (work / 'math.cpp').write_text(CPP)
    (work / 'numbers.py').write_text(PYTHON)
    source = work / 'main.poly'; source.write_text(SOURCE)
    compiler = args.polyc.resolve()
    cases = []

    def compile_case(name, src=source, flags=()):
        exe = work / name
        row = {'name': name, 'passed': False}
        row['compile'] = invoke([compiler, '--strict', '--no-package-index', '--quiet',
                                 *flags, '-o', exe, src], work)
        cases.append(row)
        assert row['compile']['returncode'] == 0, row['compile']['stderr']
        return row, exe

    try:
        for opt in range(4):
            for allocator in ('linear', 'graph'):
                name = f'O{opt}-{allocator}'
                trace = work / (name + '.jsonl')
                row, exe = compile_case(name, flags=[f'-O{opt}', '--regalloc=' + allocator,
                                                     '--trace-calls=' + str(trace)])
                for repetition in range(2):
                    row['run'] = invoke([exe], work, 10)
                    assert row['run']['returncode'] == 0 and row['run']['stderr'] == ''
                    assert row['run']['stdout'] == EXPECTED_STDOUT, row['run']['stdout']
                    row['events'] = validate(trace, source)
                row['passed'] = True
        row, exe = compile_case('disabled')
        row['run'] = invoke([exe], work, 10)
        assert row['run']['returncode'] == 0 and row['run']['stdout'] == EXPECTED_STDOUT
        assert b'__polyc_native_trace_state' not in exe.read_bytes()
        row['passed'] = True
        empty_source = work / 'empty.poly'
        empty_source.write_text('FUNC main() -> INT { RETURN 0; }\n')
        trace = work / 'empty.jsonl'
        trace.write_text('old data must be truncated\n')
        row, exe = compile_case('empty', empty_source, ['--trace-calls=' + str(trace)])
        row['run'] = invoke([exe], work, 10)
        assert row['run']['returncode'] == 0 and trace.read_text() == ''
        assert json.loads(Path(str(trace) + '.sites.json').read_text())['sites'] == []
        row['passed'] = True
        bad_sink = work / 'directory'; bad_sink.mkdir()
        row, exe = compile_case('sink-failure', empty_source, ['--trace-calls=' + str(bad_sink)])
        row['run'] = invoke([exe], work, 10)
        assert row['run']['returncode'] == 74
        assert row['run']['stderr'] == 'polyc: native call trace output failed\n'
        row['passed'] = True
    except (AssertionError, OSError, ValueError, KeyError) as error:
        if cases: cases[-1]['error'] = str(error)
    passed = len(cases) == 11 and all(row['passed'] for row in cases)
    (work / 'results.json').write_text(json.dumps({'passed': passed, 'cases': cases}, indent=2) + '\n')
    print(f'{sum(row["passed"] for row in cases)}/{len(cases)} native trace cases passed; artifacts: {work}')
    return 0 if passed else 1


if __name__ == '__main__':
    raise SystemExit(main())
