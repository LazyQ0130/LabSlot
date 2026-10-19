"""Compile the actual GUI modules into a native control integration driver.

Runs only in a fresh ignored tests/runs directory. Never reads development accounts.
No UI automation library or Python is needed by the production program.
"""
import argparse
import hashlib
from pathlib import Path
import subprocess
import time
import uuid


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--cc', default='gcc')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    run = root / 'tests' / 'runs' / ('p5-' + time.strftime('%Y%m%d-%H%M%S') + '-' + uuid.uuid4().hex[:8])
    run.mkdir(parents=True)
    protected = {p.name: (hashlib.sha256(p.read_bytes()).hexdigest(), p.stat().st_mtime_ns)
                 for p in (root / 'data').iterdir() if p.is_file()}
    binary = run / 'gui-probe.exe'
    sources = sorted(p for p in (root / 'src').glob('*.c') if p.name not in ('main.c', 'ui.c'))
    sources += sorted(p for p in (root / 'src' / 'gui').glob('*.c') if p.name != 'gui_main.c')
    command = [args.cc, '-std=c11', '-Wall', '-Wextra', '-Wpedantic', '-Werror', '-I' + str(root / 'include'),
               *map(str, sources), str(root / 'tests' / 'gui_probe.c'), str(root / 'src/gui/gui_resource.o'),
               '-lcomctl32', '-lgdi32', '-luser32', '-o', str(binary)]
    subprocess.run(command, cwd=root, check=True)
    result = subprocess.run([str(binary)], cwd=run, capture_output=True, timeout=100)
    # Only PASS/FAIL labels are printed; never persist credentials or input fields.
    output = result.stdout.decode('utf-8', errors='strict')
    status = result.returncode
    cases = [('startup-new', '--startup', None),
             ('startup-corrupt', '--startup', 'computers.txt'),
             ('startup-maintenance', '--startup', 'maintenance.txt'),
             ('account-corrupt', '--auth-error', 'students.txt'),
             ('seats-corrupt', '--seats-error', 'reservations.txt')]
    for name, mode, corrupt in cases:
        case = run / name
        data = case / 'data'
        data.mkdir(parents=True)
        # Synthetic fixtures only. Production files remain outside the test working directory.
        fixtures = {'computers.txt': ''.join(f'{i}|0\n' for i in range(1, 41)),
                    'students.txt': 'P5Student|测试同学|LocalTest9!|0\n',
                    'admins.txt': '', 'reservations.txt': '', 'error.txt': '', 'count.txt': ''}
        if corrupt:
            fixtures[corrupt] = 'invalid fixture\n'
        for filename, contents in fixtures.items():
            (data / filename).write_text(contents, encoding='utf-8')
        before = {p.name: p.read_bytes() for p in data.iterdir()}
        subcommand = [str(binary), mode]
        if mode == '--startup' and corrupt:
            subcommand.append('blocked')
        sub = subprocess.run(subcommand, cwd=case, capture_output=True, timeout=20)
        output += '\n' + name + '\n' + sub.stdout.decode('utf-8', errors='strict')
        status |= sub.returncode
        assert before == {p.name: p.read_bytes() for p in data.iterdir()}
        output += 'PASS fixture source bytes preserved\n'
    assert 'LocalTest9!' not in output
    (run / 'TEST_RESULTS.md').write_text('# P5 native GUI integration\n\n```text\n' + output + '```\n', encoding='utf-8')
    assert protected == {p.name: (hashlib.sha256(p.read_bytes()).hexdigest(), p.stat().st_mtime_ns)
                         for p in (root / 'data').iterdir() if p.is_file()}
    print(output)
    print('Development data unchanged; report:', run / 'TEST_RESULTS.md')
    if status:
        print(result.stderr.decode('utf-8', errors='replace'))
    return status


if __name__ == '__main__':
    raise SystemExit(main())
