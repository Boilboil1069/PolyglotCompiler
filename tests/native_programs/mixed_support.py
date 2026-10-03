"""Shared, dependency-free mixed-application oracle and retained test evidence."""
from __future__ import annotations
import csv
import hashlib
import io
import json
import os
from pathlib import Path
import platform
import re
import shutil
import signal
import subprocess
import time
import tempfile
import threading

ROOT = Path(__file__).resolve().parents[2]
APP = ROOT / 'examples/order_risk_analyzer'
LANGUAGES = ('poly', 'cpp', 'python', 'rust', 'go')
FIELDS = ('order_id', 'unit_price', 'quantity', 'recent_orders', 'failed_payments',
          'available_units', 'delivery_zone', 'expected_decision')


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def native_artifact(path):
    if not Path(path).is_file(): return False
    with Path(path).open('rb') as stream: header = stream.read(4)
    return header in (b'\xcf\xfa\xed\xfe', b'\xfe\xed\xfa\xcf', b'\x7fELF')


def copy_application(destination, source=APP):
    shutil.copytree(source, destination, ignore=shutil.ignore_patterns(
        'build', 'aux', '__pycache__', 'order-results.txt', '._*'))
    return {str(p.relative_to(destination)): digest(p) for p in sorted(destination.rglob('*'))
            if p.is_file() and p.suffix in ('.poly', '.cpp', '.hpp', '.py', '.rs', '.go', '.toml', '.csv')}


def invoke(command, cwd, timeout=60, *, input_text=None, memory=False, env=None):
    """Retain full output and per-command timing; timeout kills the process group."""
    command = [str(x) for x in command]
    record = {'command': command, 'cwd': str(cwd), 'peak_rss_bytes': None,
              'memory_source': 'unavailable'}
    start = time.perf_counter_ns()
    try:
        if memory and hasattr(os, 'wait4'):
            # wait4 reports this child, unlike RUSAGE_CHILDREN's cumulative
            # high-water mark. File-backed capture avoids a pipe deadlock while
            # waiting; a watchdog enforces the same process-group timeout.
            with tempfile.TemporaryFile() as stdout_file, tempfile.TemporaryFile() as stderr_file, tempfile.TemporaryFile() as stdin_file:
                if input_text is not None: stdin_file.write(input_text.encode())
                stdin_file.seek(0)
                process = subprocess.Popen(command, cwd=cwd, env=env, stdin=stdin_file,
                    stdout=stdout_file, stderr=stderr_file, start_new_session=True)
                timed_out = threading.Event()
                def kill():
                    try:
                        os.killpg(process.pid, signal.SIGKILL)
                        timed_out.set()
                    except ProcessLookupError:
                        pass
                watchdog = threading.Timer(timeout, kill); watchdog.daemon = True; watchdog.start()
                try:
                    _, status, usage = os.wait4(process.pid, 0)
                    process.returncode = os.waitstatus_to_exitcode(status)
                finally:
                    watchdog.cancel()
                stdout_file.seek(0); stderr_file.seek(0)
                stdout, stderr = stdout_file.read(), stderr_file.read()
                record.update(returncode=process.returncode, timeout=timed_out.is_set(),
                    peak_rss_bytes=int(usage.ru_maxrss) * (1 if platform.system() == 'Darwin' else 1024),
                    memory_source='wait4.ru_maxrss', user_cpu_seconds=usage.ru_utime, system_cpu_seconds=usage.ru_stime)
        else:
            process = subprocess.Popen(command, cwd=cwd, env=env,
                stdin=subprocess.PIPE if input_text is not None else subprocess.DEVNULL,
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, start_new_session=os.name != 'nt')
            try:
                stdout, stderr = process.communicate(None if input_text is None else input_text.encode(), timeout=timeout)
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
    if input_text is not None: record['stdin'] = input_text
    return record


def succeeded(record):
    return record['returncode'] == 0 and not record['timeout']


def read_rows(path):
    with Path(path).open(newline='') as stream:
        reader = csv.DictReader(stream)
        if tuple(reader.fieldnames or ()) != FIELDS:
            raise ValueError('CSV header does not match the eight-field order contract')
        return [{key: int(row[key]) for key in FIELDS} for row in reader]


def csv_text(rows):
    stream = io.StringIO(newline='')
    writer = csv.DictWriter(stream, fieldnames=FIELDS, lineterminator='\n')
    writer.writeheader(); writer.writerows(rows)
    return stream.getvalue()


def decision(row):
    """Independent integer rule oracle, evaluated before reading expected_decision."""
    price, quantity = row['unit_price'], row['quantity']
    subtotal = max(0, price * quantity) if price > 0 and quantity > 0 else 0
    discount = (24 if quantity >= 8 else 16 if quantity >= 5 else 4) if subtotal > 0 else 0
    shipping = row['delivery_zone'] + 8
    payable = shipping if subtotal <= discount else subtotal - discount + min(shipping, 15)
    recent, failed = row['recent_orders'], row['failed_payments']
    velocity = 70 if failed >= 3 else 45 if recent >= 10 else (25 if failed >= 1 else 15) if recent >= 5 else 5
    amount = 15 if payable >= 180 else 0
    score = velocity + amount + failed * 12
    risk = 3 if score >= 80 else 2 if score >= 45 else 1
    reserved = max(0, min(quantity, row['available_units'] - 5))
    payment = 40 if risk >= 3 or failed >= 2 else 20 if (risk == 2 and payable >= 150) or payable >= 220 else 1
    gate = 30 if reserved < quantity else payment
    zone = row['delivery_zone']
    base = 9 if reserved < quantity else (7 if quantity >= 8 else 5) if zone >= 3 else (5 if quantity >= 8 else 4) if zone == 2 else 2
    delay = 1 if quantity <= 4 else 2 if quantity <= 8 else 3
    eta = base + delay
    actual = gate if gate in (20, 30, 40) else 50 if gate != 1 else 20 if eta >= 10 else 40 if risk >= 3 else 100 + risk * 10 + eta
    return actual


def protocol(rows, decisions=None):
    values = [decision(row) for row in rows] if decisions is None else decisions
    stats = [len(rows), sum(v >= 100 for v in values), values.count(20), values.count(40), values.count(30), sum(values)]
    return ''.join(f'ORDER {row["order_id"]} {value}\n' for row, value in zip(rows, values)) + 'SUMMARY ' + ' '.join(map(str, stats)) + '\n'


def scenarios(app):
    original = read_rows(app / 'data/orders.csv')
    if not original or any(decision(row) != row['expected_decision'] for row in original):
        raise ValueError('Checked-in order expectations disagree with the independent rule oracle')
    changed = [dict(row) for row in original]
    changed[0]['quantity'] += 1
    changed[0]['expected_decision'] = decision(changed[0])
    if protocol(original) == protocol(changed):
        raise ValueError('Sensitivity fixture must change the complete output')
    extended = [dict(row, order_id=row['order_id'] + batch * 10000) for batch in range(3) for row in original]
    wrong = [dict(row) for row in changed]
    wrong[0]['expected_decision'] = original[0]['expected_decision']
    invalid = [dict(original[0], quantity=0)]
    good = [('baseline', original), ('changed-input', changed), ('extended-input', extended), ('reordered-input', list(reversed(original)))]
    result = [{'name': name, 'input': csv_text(rows), 'rows': rows, 'exit': 0,
               'expected_stdout': protocol(rows), 'expected_file': protocol(rows)} for name, rows in good]
    result += [
        {'name': 'wrong-expectation', 'input': csv_text(wrong), 'exit': 225},
        {'name': 'truncated-record', 'input': ','.join(FIELDS) + '\n' + ','.join(str(original[0][key]) for key in FIELDS[:-1]) + '\n', 'exit': 222},
        {'name': 'invalid-quantity', 'input': csv_text(invalid), 'exit': 222},
        {'name': 'empty-input', 'input': ','.join(FIELDS) + '\n', 'exit': 222},
        {'name': 'missing-input', 'input': None, 'exit': 220, 'expected_file': None},
        {'name': 'output-is-directory', 'input': csv_text(original), 'exit': 227, 'output_is_directory': True, 'expected_file': None},
    ]
    for item in result:
        item.setdefault('expected_stdout', '')
        item.setdefault('expected_file', '')
    return result


def scaled_scenarios(original, row_counts):
    """Repeat the eight rule cases with unique IDs, recomputing every expectation.

    This measures input-volume scaling at a fixed rule mix, not new rule coverage.
    The original-size scenario deliberately matches the acceptance baseline so its
    already validated execution can be reused without running it twice.
    """
    if not original: raise ValueError('scale source must contain orders')
    stride = max(row['order_id'] for row in original) - min(row['order_id'] for row in original) + 1
    result = []
    for count in row_counts:
        if count < 1: raise ValueError('row count must be positive')
        rows = []
        for index in range(count):
            row = dict(original[index % len(original)])
            row['order_id'] += (index // len(original)) * stride
            row['expected_decision'] = decision(row)
            rows.append(row)
        input_text, expected = csv_text(rows), protocol(rows)
        result.append({'name': f'rows-{count}', 'row_count': count, 'input': input_text,
                       'input_sha256': hashlib.sha256(input_text.encode()).hexdigest(),
                       'expected_stdout': expected, 'expected_file': expected, 'exit': 0})
    return result


def validate_build_report(path, opt, allocator):
    errors = []
    try: report = json.loads(path.read_text())
    except (OSError, ValueError) as error: return None, ['build report missing or invalid: ' + str(error)]
    if report.get('schema') != 'polyglot.native-build.v1': errors.append('unexpected build-report schema')
    modules = report.get('modules', [])
    seen = {module.get('language') for module in modules}
    if not set(LANGUAGES) <= seen: errors.append('missing module languages: ' + ', '.join(sorted(set(LANGUAGES) - seen)))
    aliases = {'linear': 'linear', 'linear-scan': 'linear', 'graph': 'graph', 'graph-coloring': 'graph'}
    for module in modules:
        prefix = str(module.get('source', '<missing source>')) + ': '
        if module.get('opt_level') != opt: errors.append(prefix + 'optimization setting was not inherited')
        if aliases.get(module.get('regalloc')) != aliases[allocator]: errors.append(prefix + 'allocator setting was not inherited')
        if module.get('status') != 'succeeded': errors.append(prefix + 'module did not succeed')
        if not isinstance(module.get('elapsed_ms'), (int, float)) or module['elapsed_ms'] < 0: errors.append(prefix + 'invalid elapsed time')
        if not module.get('object'): errors.append(prefix + 'missing object path')
    return report, errors


def run_scenarios(executable, folder, cases, timeout, *, memory=False):
    records = []
    for case in cases:
        work = folder / case['name']; work.mkdir(parents=True)
        input_path, output_path = work / 'orders.csv', work / 'orders-result.txt'
        if case['input'] is not None: input_path.write_text(case['input'])
        if case.get('output_is_directory'): output_path.mkdir()
        run = invoke([executable, input_path, output_path], work, timeout, memory=memory)
        file_text = output_path.read_text() if output_path.is_file() else None
        passed = (run['returncode'] == case['exit'] and not run['timeout'] and run['stderr'] == ''
                  and run['stdout'] == case['expected_stdout'] and file_text == case['expected_file'])
        records.append({'name': case['name'], 'passed': passed, 'expected_exit': case['exit'],
                        'expected_stdout': case['expected_stdout'], 'expected_file': case['expected_file'],
                        'file_contents': file_text, 'run': run})
    return records


def locate(name):
    for candidate in [shutil.which(name), *[str(Path(p) / name) for p in (
            '/opt/homebrew/bin', '/usr/local/bin', '/usr/local/go/bin', str(Path.home() / '.cargo/bin'))]]:
        if candidate and Path(candidate).is_file(): return candidate
    return None


def official_references(app, work, cases, timeout=90):
    """Compile original four modules with official tools, then compose their results."""
    work.mkdir(parents=True)
    commands = {'cpp': (locate('clang++') or locate('g++'), '--version'),
                'python': (locate('python3'), '--version'), 'rust': (locate('rustc'), '--version'),
                'go': (locate('go'), 'version')}
    tools, runners = {}, {}
    for language, (path, flag) in commands.items():
        record = {'path': path, 'available': False}
        if path:
            record['probe'] = invoke([path, flag], work, timeout)
            record['available'] = succeeded(record['probe'])
        else: record['reason'] = 'official toolchain not installed or not discoverable'
        tools[language] = record
    if not all(record['available'] for record in tools.values()):
        return {'status': 'missing', 'tools': tools, 'scenarios': []}
    cpp = (app / 'cpp/pricing_engine.cpp').read_text() + r'''
#include <iostream>
int main() { int p,q,z; while (std::cin >> p >> q >> z) {
  int s=pricing_subtotal(p,q), d=pricing_discount(s,q,2);
  std::cout << s << ' ' << d << ' ' << pricing_payable(s,d,z+8) << ' '
            << pricing_session_payable(p,q,z) << '\n';
} }
'''
    python = (app / 'packages/fraud_policy/src/fraud_policy.py').read_text() + '\n' + (app / 'python/fraud_engine.py').read_text() + '''
import sys
for line in sys.stdin:
    recent, failed, payable = map(int, line.split())
    velocity = fraud_velocity_points(recent, failed, 1)
    amount = fraud_amount_points(payable, 28, 0)
    print(velocity, amount, fraud_risk_band(velocity, amount, failed), fraud_session_band(velocity, amount, failed))
'''
    rust = (app / 'packages/fulfillment_policy/src/fulfillment_policy.rs').read_text() + '\n' + (app / 'rust/fulfillment_engine.rs').read_text() + r'''
use std::io::{self, Read};
fn main() { let mut input=String::new(); io::stdin().read_to_string(&mut input).unwrap();
let values:Vec<i64>=input.split_whitespace().map(|x| x.parse().unwrap()).collect();
for row in values.chunks_exact(5) { let reserved=inventory_reservable(row[0],row[1],5);
let payment=payment_authorization(row[2],row[3],row[4]);
println!("{} {} {} {}",reserved,payment,fulfillment_gate(row[0],reserved,payment),fulfillment_session_gate(row[0],reserved,payment)); } }
'''
    go_parts = [(app / 'packages/logistics_policy/src/logistics_policy.go').read_text(), (app / 'go/logistics_engine.go').read_text()]
    go = 'package main\nimport ("fmt"; "bufio"; "os")\n' + '\n'.join(re.sub(r'(?m)^package\s+\w+\s*$', '', part) for part in go_parts) + r'''
func main() { input:=bufio.NewReader(os.Stdin); for { var zone,quantity,reserved,gate,risk int
if _,err:=fmt.Fscan(input,&zone,&quantity,&reserved,&gate,&risk); err!=nil { break }
base:=logistics_base_days(zone,quantity,reserved); delay:=logistics_capacity_delay(quantity,4,1); eta:=base+delay
fmt.Println(base,delay,eta,logistics_decision(gate,risk,eta),logistics_session_decision(gate,risk,eta)) } }
'''
    for language, source, extension in [('cpp', cpp, 'cpp'), ('python', python, 'py'), ('rust', rust, 'rs'), ('go', go, 'go')]:
        src = work / ('reference.' + extension); src.write_text(source)
        exe = work / ('reference-' + language)
        tool = tools[language]['path']
        env = os.environ.copy()
        if language == 'cpp': command = [tool, '-std=c++17', '-O0', '-I', app / 'packages/order_policy/include', src, '-o', exe]
        elif language == 'rust': command = [tool, '--crate-name', 'order_reference', src, '-o', exe]
        elif language == 'go':
            env.update(GOCACHE=str(work / 'go-cache'), GOPATH=str(work / 'go-path'), GO111MODULE='off', GOPROXY='off')
            command = [tool, 'build', '-o', exe, src]
        else: command = None
        tools[language]['source_sha256'] = digest(src)
        if command:
            tools[language]['compile'] = invoke(command, work, timeout, env=env)
            if not succeeded(tools[language]['compile']):
                return {'status': 'failed', 'tools': tools, 'scenarios': []}
        runners[language] = [tool, src] if language == 'python' else [exe]
    records = []
    for case in cases:
        if case['exit'] != 0: continue
        rows = case['rows']; stages = {}
        def stage(lang, values, width):
            result = invoke(runners[lang], work, timeout, input_text=''.join(' '.join(map(str, row)) + '\n' for row in values))
            stages[lang] = result
            output = [[int(value) for value in line.split()] for line in result['stdout'].splitlines()]
            if not succeeded(result) or result['stderr'] or len(output) != len(rows) or any(len(row) != width for row in output):
                raise ValueError(lang + ' official module execution failed')
            if any(row[-1] != row[-2] for row in output):
                raise ValueError(lang + ' object and free-function results disagree')
            return output
        try:
            pricing = stage('cpp', [[r['unit_price'], r['quantity'], r['delivery_zone']] for r in rows], 4)
            fraud = stage('python', [[r['recent_orders'], r['failed_payments'], p[2]] for r, p in zip(rows, pricing)], 4)
            fulfillment = stage('rust', [[r['quantity'], r['available_units'], f[2], p[2], r['failed_payments']] for r, f, p in zip(rows, fraud, pricing)], 4)
            logistics = stage('go', [[r['delivery_zone'], r['quantity'], u[0], u[2], f[2]] for r, u, f in zip(rows, fulfillment, fraud)], 5)
            actual = protocol(rows, [row[3] for row in logistics])
            records.append({'name': case['name'], 'stages': stages, 'stdout': actual, 'passed': actual == case['expected_stdout']})
        except (ValueError, IndexError) as error:
            records.append({'name': case['name'], 'stages': stages, 'passed': False, 'error': str(error)})
    return {'status': 'passed' if records and all(record['passed'] for record in records) else 'failed',
            'tools': tools, 'scenarios': records}
