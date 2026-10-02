"""Deterministic native programs with independently computed exit-status oracles."""
LANGUAGES = {'poly': 'poly', 'cpp': 'cpp', 'python': 'py', 'rust': 'rs',
             'go': 'go', 'java': 'java', 'dotnet': 'cs', 'javascript': 'js', 'ruby': 'rb'}


def function(language, name, params, body, boolean=False):
    """body uses simple C-like statements, or a language-specific block."""
    if language == 'poly':
        return f'FUNC {name}({", ".join(p + ": INT" for p in params)}) -> INT {{\n{body}\n}}\n'
    if language == 'cpp':
        return f'long {name}({", ".join("long " + p for p in params)}) {{\n{body}\n}}\n'
    if language == 'rust':
        return f'fn {name}({", ".join(p + ": i64" for p in params)}) -> i64 {{\n{body}\n}}\n'
    if language == 'go':
        return f'func {name}({", ".join(p + " int" for p in params)}) int {{\n{body}\n}}\n'
    if language in ('java', 'dotnet'):
        return f'static int {name}({", ".join("int " + p for p in params)}) {{\n{body}\n}}\n'
    if language == 'python':
        return f'def {name}({", ".join(p + ": int" for p in params)}) -> int:\n' + '\n'.join('    ' + s for s in body.splitlines()) + '\n'
    if language == 'ruby':
        doc = ''.join(f'# @param {p} [Integer]\n' for p in params) + '# @return [Integer]\n'
        return doc + f'def {name}({", ".join(params)})\n{body}\nend\n'
    doc = '/**\n' + ''.join(f' * @param {{number}} {p}\n' for p in params)
    doc += ' * @returns {' + ('boolean' if boolean else 'number') + '}\n */\n'
    return doc + f'function {name}({", ".join(params)}) {{\n{body}\n}}\n'


def ret(language, expr):
    return ('RETURN ' if language == 'poly' else 'return ') + expr + ('' if language in ('python', 'ruby') else ';')


def program(language, case):
    parts = []
    if case.startswith('scale_'):
        count = int(case.split('_')[1])
        parts.append(function(language, 'step0', ['x'], ret(language, 'x + 1')))
        for i in range(1, count):
            parts.append(function(language, f'step{i}', ['x'], ret(language, f'step{i-1}(x) + 1')))
        value, expected = f'step{count-1}(7)', (count + 7) % 256
        numeric = count + 7
    elif case == 'calls':
        parts.append(function(language, 'twice', ['x'], ret(language, 'x * 2')))
        parts.append(function(language, 'combine', ['x', 'y'], ret(language, 'twice(x) + twice(y)')))
        value, expected, numeric = 'combine(13, 8)', 42, 42
    elif case == 'recursion':
        if language == 'python': body = 'if n <= 1:\n    return n\nreturn fib(n - 1) + fib(n - 2)'
        elif language == 'ruby': body = 'if n <= 1\n  return n\nend\nreturn fib(n - 1) + fib(n - 2)'
        elif language == 'poly': body = 'IF n <= 1 { RETURN n; }\nRETURN fib(n - 1) + fib(n - 2);'
        else: body = 'if (n <= 1) { return n; }\nreturn fib(n - 1) + fib(n - 2);'
        parts.append(function(language, 'fib', ['n'], body))
        value, expected, numeric = 'fib(10)', 55, 55
    elif case == 'control_flow':
        bodies = {
            'python': 'total = 0\ni = 0\nwhile i < n:\n    if i < 5:\n        total = total + i\n    else:\n        total = total + 2\n    i = i + 1\nreturn total',
            'ruby': 'total = 0\ni = 0\nwhile i < n\n  if i < 5\n    total = total + i\n  else\n    total = total + 2\n  end\n  i = i + 1\nend\nreturn total',
            'poly': 'VAR total = 0; VAR i = 0;\nWHILE i < n { IF i < 5 { total = total + i; } ELSE { total = total + 2; } i = i + 1; }\nRETURN total;',
        }
        body = bodies.get(language)
        if body is None:
            declaration = {'rust': 'let mut total: i64 = 0; let mut i: i64 = 0;',
                           'go': 'var total int = 0; var i int = 0;',
                           'javascript': 'let total = 0; let i = 0;'}.get(language, 'int total = 0; int i = 0;')
            loop = 'for' if language == 'go' else 'while'
            body = declaration + f'\n{loop} (i < n) {{ if (i < 5) {{ total = total + i; }} else {{ total = total + 2; }} i = i + 1; }}\nreturn total;'
        parts.append(function(language, 'accumulate', ['n'], body))
        value, expected, numeric = 'accumulate(20)', 40, 40
    else:
        raise ValueError(case)
    name = 'Main' if language == 'dotnet' else 'main'
    # JavaScript's source numbers are doubles. Its native entry returns a
    # boolean checksum, avoiding any invented float-to-process-status cast.
    expr = f'{value} === {numeric}' if language == 'javascript' else value
    parts.append(function(language, name, [], ret(language, expr), language == 'javascript'))
    source = '\n'.join(parts)
    if language in ('java', 'dotnet'): source = 'class Program {\n' + source + '}\n'
    if language == 'go': source = 'package main\n' + source
    return source, 1 if language == 'javascript' else expected
