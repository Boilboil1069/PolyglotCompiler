#!/usr/bin/env python3
"""Execute standard C++, Java, C#, Rust and Go entry signatures."""
import argparse
import json
import sys
import tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from differential_native import invoke

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--polyc', type=Path, required=True)
    p.add_argument('--output-root', type=Path, required=True)
    a = p.parse_args()
    a.output_root.mkdir(parents=True, exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix='entries-', dir=a.output_root.resolve()))
    rows = []
    for src in sorted((ROOT / 'tests/native_programs/entries').glob('standard.*')):
        for opt in range(4):
            exe = out / (src.suffix[1:] + f'-O{opt}')
            row = {'source': str(src), 'opt': opt, 'passed': False}
            row['compile'] = invoke([a.polyc.resolve(), '--strict', '--no-package-index', '--no-aux', '--quiet', f'-O{opt}', src, '-o', exe], out)
            if row['compile']['returncode'] == 0:
                row['run'] = invoke([exe, 'entry-works'], out)
                row['passed'] = row['run']['returncode'] == 0 and row['run']['stdout'] == '2\nentry-works\n' and not row['run']['stderr']
            rows.append(row)
    (out / 'results.json').write_text(json.dumps(rows, indent=2) + '\n')
    print(f'{sum(r["passed"] for r in rows)}/{len(rows)} standard entry checks passed; {out}')
    for r in rows:
        if not r['passed']: print(json.dumps(r))
    return int(not all(r['passed'] for r in rows))
if __name__ == '__main__': raise SystemExit(main())
