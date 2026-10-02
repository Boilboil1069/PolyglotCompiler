"""Shared kernels; only entry and output adapters differ between toolchains.

This is a tested scalar subset, not a conformance suite for whole languages.
No official Poly implementation exists, so Poly uses independent value oracles.
"""
from corpus import LANGUAGES

CASES = ('signed_values', 'wide_values', 'nested_calls', 'nested_loops',
         'recursion', 'parameter_update', 'short_circuit', 'float_values')


def function(lang, name, params, body, floating=False):
    if lang == 'poly':
        t = 'FLOAT' if floating else 'INT'
        return f'FUNC {name}({", ".join(p + ": " + t for p in params)}) -> {t} {{\n{body}\n}}\n'
    if lang == 'python':
        t = 'float' if floating else 'int'
        return f'def {name}({", ".join(p + ": " + t for p in params)}) -> {t}:\n' + '\n'.join('    ' + s for s in body.splitlines()) + '\n'
    if lang == 'ruby':
        t = 'Float' if floating else 'Integer'
        return ''.join(f'# @param {p} [{t}]\n' for p in params) + f'# @return [{t}]\ndef {name}({", ".join(params)})\n{body}\nend\n'
    if lang == 'javascript':
        doc = '/**\n' + ''.join(f' * @param {{number}} {p}\n' for p in params) + ' * @returns {number}\n */\n'
        return doc + f'function {name}({", ".join(params)}) {{\n{body}\n}}\n'
    if lang == 'rust':
        t = 'f64' if floating else 'i64'
        return f'fn {name}({", ".join(p + ": " + t for p in params)}) -> {t} {{\n{body}\n}}\n'
    if lang == 'go':
        t = 'float64' if floating else 'int64'
        return f'func {name}({", ".join(p + " " + t for p in params)}) {t} {{\n{body}\n}}\n'
    t = 'double' if floating else 'long'
    return ('static ' if lang in ('java', 'dotnet') else '') + f'{t} {name}({", ".join(t + " " + p for p in params)}) {{\n{body}\n}}\n'


def ret(lang, expression):
    return ('RETURN ' if lang == 'poly' else 'return ') + expression + ('' if lang in ('python', 'ruby') else ';')


def statement(lang, expression):
    return expression + ('' if lang in ('python', 'ruby') else ';')


def local(lang, name, value):
    declaration = {'poly': f'VAR {name} = ', 'rust': f'let mut {name}: i64 = ',
                   'go': f'var {name} int64 = ', 'javascript': f'let {name} = ',
                   'cpp': f'long {name} = ', 'java': f'long {name} = ', 'dotnet': f'long {name} = '}
    return statement(lang, declaration.get(lang, name + ' = ') + str(value))


def conditional(lang, condition, body):
    if lang == 'python': return 'if ' + condition + ':\n' + '\n'.join('    ' + line for line in body.splitlines())
    if lang == 'ruby': return 'if ' + condition + '\n' + body + '\nend'
    return ('IF ' if lang == 'poly' else 'if (') + condition + (' {' if lang == 'poly' else ') {') + '\n' + body + '\n}'


def loop(lang, condition, body):
    if lang == 'python': return 'while ' + condition + ':\n' + '\n'.join('    ' + line for line in body.splitlines())
    if lang == 'ruby': return 'while ' + condition + '\n' + body + '\nend'
    return ('WHILE ' if lang == 'poly' else 'for (' if lang == 'go' else 'while (') + condition + (' {' if lang == 'poly' else ') {') + '\n' + body + '\n}'


def source(lang, case):
    number = lang == 'javascript' or case == 'float_values'
    output = 'print_f64' if number else 'print_i64'
    if case == 'signed_values':
        kernels = function(lang, 'evaluate', ['a', 'b'], ret(lang, '(a - b) * (a + b)'))
        calls, expected = ['evaluate(-12345, 6789)', 'evaluate(0, 99)', 'evaluate(-19, -23)'], [106308504, -9801, -168]
    elif case == 'wide_values':
        kernels = function(lang, 'evaluate', ['a'], ret(lang, 'a * a * 64 + 17'))
        calls, expected = ['evaluate(1234567)', 'evaluate(-7654321)'], [1234567**2*64+17, (-7654321)**2*64+17]
    elif case == 'nested_calls':
        kernels = function(lang, 'twice', ['x'], ret(lang, 'x * 2'))
        kernels += function(lang, 'evaluate', ['x', 'y'], ret(lang, 'twice(x) + twice(y) * 3'))
        calls, expected = ['evaluate(-31, 49)', 'evaluate(1234567, -8)'], [232, 2469086]
    elif case == 'recursion':
        body = conditional(lang, 'n <= 1', ret(lang, 'n')) + '\n' + ret(lang, 'fib(n - 1) + fib(n - 2)')
        kernels = function(lang, 'fib', ['n'], body)
        calls, expected = ['fib(12)', 'fib(17)'], [144, 1597]
    elif case == 'parameter_update':
        # Rust bindings are immutable by default; a local copy is explicit in
        # that kernel. Other languages update the ordinary parameter binding.
        name = 'value' if lang in ('rust', 'poly') else 'x'
        body = (local(lang, 'value', 'x') + '\n') if lang in ('rust', 'poly') else ''
        body += statement(lang, f'{name} = {name} + 7') + '\n'
        body += loop(lang, f'{name} < 100', statement(lang, f'{name} = {name} + 9')) + '\n'
        body += ret(lang, f'{name} * 5')
        kernels = function(lang, 'evaluate', ['x'], body)
        calls, expected = ['evaluate(-31)', 'evaluate(500)'], [510, 2535]
    elif case == 'nested_loops':
        inner = conditional(lang, 'j < 3', statement(lang, 'total = total + i * 3 + j')) + '\n' + statement(lang, 'j = j + 1')
        outer = local(lang, 'j', 0) + '\n' + loop(lang, 'j < n', inner) + '\n' + statement(lang, 'i = i + 1')
        body = local(lang, 'total', 0) + '\n' + local(lang, 'i', 0) + '\n' + loop(lang, 'i < n', outer) + '\n' + ret(lang, 'total')
        kernels = function(lang, 'evaluate', ['n'], body)
        calls, expected = ['evaluate(9)', 'evaluate(17)'], [351, 1275]
    elif case == 'short_circuit':
        marker = statement(lang, f'{output}(x)') + '\n' + ret(lang, '1')
        kernels = function(lang, 'marker', ['x'], marker)
        conjunction = ' AND ' if lang == 'poly' else ' and ' if lang == 'python' else ' && '
        body = conditional(lang, 'x < 0' + conjunction + 'marker(x) > 0', ret(lang, '7')) + '\n' + ret(lang, '9')
        kernels += function(lang, 'evaluate', ['x'], body)
        calls, expected = ['evaluate(42)', 'evaluate(-3)'], [9, -3, 7]
    elif case == 'float_values':
        kernels = function(lang, 'evaluate', ['a', 'b'], ret(lang, '(a + b) * (a - b) / 2.0'), True)
        calls, expected = ['evaluate(3.5, 1.25)', 'evaluate(-17.125, 9.25)', 'evaluate(0.0, 0.0)'], [5.34375, 103.8515625, 0.0]
    else: raise ValueError(case)
    wrapper_body = '\n'.join(statement(lang, f'{output}({call})') for call in calls)
    if lang == 'javascript':
        native_entry = '/** @returns {boolean} */\nfunction main() {\n' + wrapper_body + '\nreturn true;\n}\n'
        native_exit = 1
    else:
        native_entry = function(lang, 'Main' if lang == 'dotnet' else 'main', [], wrapper_body + '\n' + ret(lang, '0'))
        native_exit = 0
    native = kernels + native_entry
    if lang in ('java', 'dotnet'): native = 'class Program {\n' + native + '\n}\n'
    if lang == 'go': native = 'package main\n' + native
    # The reference compiler receives exactly kernels, plus this I/O + entry
    # adapter. No edits to operations or control flow are made in the kernels.
    if lang == 'cpp':
        reference = '#include <iostream>\n#include <iomanip>\nvoid print_i64(long x) { std::cout << x << "\\n"; }\nvoid print_f64(double x) { std::cout << std::setprecision(17) << x << "\\n"; }\n' + kernels + 'int main() {\n' + wrapper_body + '\nreturn 0;\n}\n'
    elif lang == 'python': reference = 'def print_i64(x): print(x)\ndef print_f64(x): print(repr(x))\n' + kernels + '\n'.join(f'{output}({call})' for call in calls) + '\n'
    elif lang == 'ruby': reference = 'def print_i64(x)\nputs x\nend\ndef print_f64(x)\nputs x\nend\n' + kernels + wrapper_body + '\n'
    elif lang == 'javascript': reference = 'function print_f64(x) { console.log(x); }\n' + kernels + wrapper_body + '\n'
    elif lang == 'rust': reference = 'fn print_i64(x: i64) { println!("{}", x); }\nfn print_f64(x: f64) { println!("{:.17}", x); }\n' + kernels + 'fn main() {\n' + wrapper_body + '\n}\n'
    elif lang == 'go': reference = 'package main\nimport "fmt"\nfunc print_i64(x int64) { fmt.Println(x) }\nfunc print_f64(x float64) { fmt.Printf("%.17g\\n",x) }\n' + kernels + 'func main() {\n' + wrapper_body + '\n}\n'
    elif lang == 'java': reference = 'class Program {\nstatic void print_i64(long x) { System.out.println(x); }\nstatic void print_f64(double x) { System.out.println(x); }\n' + kernels + 'public static void main(String[] args) {\n' + wrapper_body + '\n}\n}\n'
    elif lang == 'dotnet': reference = 'using System;\nusing System.Globalization;\nclass Program {\nstatic void print_i64(long x) { Console.WriteLine(x); }\nstatic void print_f64(double x) { Console.WriteLine(x.ToString("R", CultureInfo.InvariantCulture)); }\n' + kernels + 'static void Main() {\n' + wrapper_body + '\n}\n}\n'
    else: reference = None
    return {'native': native, 'reference': reference, 'kernel': kernels, 'kind': 'f64' if number else 'i64',
            'expected': expected, 'native_exit': native_exit}
