"""P5.4 real-clock GUI end-to-end, actual system DPI and safe demo preparation."""
import argparse
from datetime import date, timedelta
import hashlib
from pathlib import Path
import re
import subprocess
import sys
import time
import uuid


def snapshot(directory):
    return {p.name: (hashlib.sha256(p.read_bytes()).hexdigest(), p.stat().st_mtime_ns)
            for p in directory.iterdir() if p.is_file()}


def visual_fixture(directory):
    """Deterministic QA extremes, separate from the GUI-created end-to-end data."""
    directory.mkdir(parents=True)
    ids = ['U1', 'U2'] + [f'T{i:02d}' for i in range(3, 41)]
    today = date.today()
    monday = today - timedelta(days=today.weekday())
    files = {'students.txt': ''.join(f'{sid}|合成测试学生{i:02d}|VisualTest9!|0\n' for i, sid in enumerate(ids, 1)),
             'admins.txt': 'A1|合成管理员|VisualTest9!\n',
             'computers.txt': ''.join(f'{i}|{int(i == 2)}\n' for i in range(1, 41)),
             'count.txt': '',
             'error.txt': (f'1|U1|3|{today:%Y%m%d}|显示器闪烁 Display A 测试|0\n'
                           f'2|U1|4|{today:%Y%m%d}|已处理的键盘故障|1\n'
                           f'3|U2|2|{today:%Y%m%d}|请检查维修结果|0\n')}
    rows = []
    for day in range(7):
        counts = [40, 1, 3, 7, 12, 20, 28, 35, 0] if day == today.weekday() else [40] * 9 if day == 2 else [0] * 9 if day == 1 else [8] * 9
        for slot, count in enumerate(counts):
            for i in range(count):
                rows.append(f'{len(rows)+1}|{ids[i]}|{i+1}|{monday+timedelta(days=day):%Y%m%d}|{slot}|1\n')
    files['reservations.txt'] = ''.join(rows)
    for name, content in files.items():
        (directory / name).write_text(content, encoding='utf-8', newline='\n')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--cc', default='gcc')
    parser.add_argument('--expect-dpi', type=int)
    parser.add_argument('--prepare-view', action='store_true', help='Build and prepare isolated QA extremes without running the end-to-end test.')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    run = root / 'tests/runs' / ('p5-4-' + time.strftime('%Y%m%d-%H%M%S') + '-' + uuid.uuid4().hex[:8])
    run.mkdir(parents=True)
    protected = snapshot(root / 'data')
    sources = sorted(p for p in (root / 'src').glob('*.c') if p.name not in ('main.c', 'ui.c'))
    sources += sorted(p for p in (root / 'src/gui').glob('*.c') if p.name != 'gui_main.c')
    binary = run / 'gui-final-probe.exe'
    subprocess.run([args.cc, '-std=c11', '-Wall', '-Wextra', '-Wpedantic', '-Werror', '-I' + str(root / 'include'),
                    *map(str, sources), str(root / 'tests/gui_final_probe.c'), str(root / 'src/gui/gui_resource.o'),
                    '-lcomctl32', '-lgdi32', '-luser32', '-o', str(binary)], check=True)
    if args.prepare_view:
        visual_fixture(run / 'visual/data')
        print('Inspection binary:', binary)
        print('QA fixture directory:', run / 'visual')
        return 0
    case = run / 'end-to-end'
    case.mkdir()
    with (case / 'stdout.log').open('wb') as out, (case / 'stderr.log').open('wb') as err:
        result = subprocess.run([str(binary)], cwd=case, stdout=out, stderr=err, timeout=180)
    output = (case / 'stdout.log').read_text(encoding='utf-8').replace('\r\n', '\n')
    assert 'FinalTest9!' not in output and 'VisualTest9!' not in output
    print(output, end='', flush=True)
    if result.returncode:
        print((case / 'stderr.log').read_text(encoding='utf-8', errors='replace'))
    actual = re.search(r'ACTUAL_SYSTEM_DPI (\d+)', output)
    if args.expect_dpi is not None:
        assert actual and int(actual[1]) == args.expect_dpi, 'The requested actual system DPI was not observed.'
        output += f'PASS requested real system DPI {args.expect_dpi} observed without message simulation\n'
    target = run / 'prepared-demo'
    command = [sys.executable, str(root / 'scripts/prepare_demo.py'), '--target', str(target)]
    declined = subprocess.run(command, input='not confirmed\n', text=True, capture_output=True)
    assert declined.returncode == 2 and not target.exists()
    output += 'PASS unconfirmed demo preparation creates nothing\n'
    subprocess.run([*command, '--confirm-new-directory'], check=True, capture_output=True)
    assert not (target / 'data').exists() and (target / 'DEMO_SESSION.md').is_file()
    assert (target / 'LabSlot-GUI.exe').read_bytes() == (root / 'LabSlot-GUI.exe').read_bytes()
    assert (target / 'smart-lab.exe').read_bytes() == (root / 'smart-lab.exe').read_bytes()
    output += 'PASS confirmed new demo copies exact production binaries / no accounts or data\n'
    before = snapshot(target)
    existing = subprocess.run([*command, '--confirm-new-directory'], capture_output=True)
    assert existing.returncode == 2 and before == snapshot(target)
    output += 'PASS existing demonstration directory never overwritten\n'
    protected_target = root / 'data' / ('prohibited-demo-' + uuid.uuid4().hex)
    forbidden = subprocess.run([sys.executable, str(root / 'scripts/prepare_demo.py'), '--target', str(protected_target), '--confirm-new-directory'], capture_output=True)
    assert forbidden.returncode == 2 and not protected_target.exists()
    output += 'PASS development data cannot be initialized as a demonstration\n'
    relative = subprocess.run([sys.executable, str(root / 'scripts/prepare_demo.py'), '--target', 'relative-demo', '--confirm-new-directory'], cwd=case, capture_output=True)
    assert relative.returncode == 2 and not (case / 'relative-demo').exists()
    output += 'PASS relative demo target rejected\n'
    assert protected == snapshot(root / 'data')
    output += 'PASS real development data hashes and modification times unchanged\n'
    (run / 'TEST_RESULTS.md').write_text('# P5.4 real Windows GUI validation\n\n```text\n' + output + '```\n', encoding='utf-8', newline='\n')
    passed = sum(line.startswith('PASS ') for line in output.splitlines())
    failures = sum(line.startswith('FAIL ') for line in output.splitlines())
    print(f'{passed} passed, {failures} failed; report: {run / "TEST_RESULTS.md"}')
    return int(result.returncode != 0 or failures != 0)


if __name__ == '__main__':
    raise SystemExit(main())
