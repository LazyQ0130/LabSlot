"""P5.2 real Win32 control/message tests, with isolated fixtures and a test-only clock."""
import argparse
import hashlib
from pathlib import Path
import subprocess
import time
import uuid


def snapshot(directory):
    return {p.name: (hashlib.sha256(p.read_bytes()).hexdigest(), p.stat().st_mtime_ns)
            for p in directory.iterdir() if p.is_file()}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--cc', default='gcc')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    run = root / 'tests/runs' / ('p5-2-' + time.strftime('%Y%m%d-%H%M%S') + '-' + uuid.uuid4().hex[:8])
    run.mkdir(parents=True)
    protected = snapshot(root / 'data')
    flags = [args.cc, '-std=c11', '-Wall', '-Wextra', '-Wpedantic', '-Werror', '-I' + str(root / 'include')]
    clock_object = run / 'utils-test-clock.o'
    subprocess.run([*flags, '-Dutils_local_time=gui_real_local_time', '-c', str(root / 'src/utils.c'), '-o', str(clock_object)], check=True)
    sources = sorted(p for p in (root / 'src').glob('*.c') if p.name not in ('main.c', 'ui.c', 'utils.c'))
    sources += sorted(p for p in (root / 'src/gui').glob('*.c') if p.name != 'gui_main.c')
    binary = run / 'gui-student-probe.exe'
    subprocess.run([*flags, *map(str, sources), str(root / 'tests/gui_student_probe.c'), str(clock_object),
                    str(root / 'src/gui/gui_resource.o'), '-lcomctl32', '-lgdi32', '-luser32', '-o', str(binary)], check=True)
    bookings = ('1|U1|1|20261020|0|1\n2|U1|3|20261018|0|1\n3|U1|4|20261020|1|0\n'
                '4|U2|5|20261020|0|1\n5|U1|6|20261019|3|1\n6|U1|7|20261019|1|1\n')
    reports = '1|U1|3|20261019|显示器闪烁|0\n2|U1|4|20261018|键盘无响应|1\n3|U2|5|20261019|PRIVATE_OTHER|0\n'
    results = []
    failed = False
    for scenario in ['normal', 'empty', 'boundary', 'changed', 'corrupt-reservations', 'corrupt-reports', 'save-failure', 'account-gone', 'large']:
        print('Running', scenario, flush=True)
        case = run / scenario
        data = case / 'data'
        data.mkdir(parents=True)
        fixtures = {'students.txt': 'U1|合成学生|StudentTest9!|999\nU2|Other|StudentTest9!|0\n',
                    'admins.txt': 'A1|合成管理员|StudentTest9!\n',
                    'computers.txt': ''.join(f'{i}|{int(i == 2)}\n' for i in range(1, 41)),
                    'reservations.txt': bookings, 'error.txt': reports, 'count.txt': ''}
        if scenario == 'empty':
            fixtures['reservations.txt'] = fixtures['error.txt'] = ''
        if scenario == 'corrupt-reservations':
            fixtures['reservations.txt'] = 'corrupt fixture\n'
        if scenario == 'corrupt-reports':
            fixtures['error.txt'] = 'corrupt fixture\n'
        if scenario == 'save-failure':
            fixtures['error.txt.tmp'] = 'exclusive temporary path is deliberately occupied\n'
        if scenario == 'large':
            fixtures['reservations.txt'] = ''.join(f'{i}|{"U2" if i == 1 else "U1"}|1|20261018|0|0\n' for i in range(1, 100001))
        for filename, content in fixtures.items():
            (data / filename).write_text(content, encoding='utf-8', newline='\n')
        before = snapshot(data)
        with (case / 'stdout.log').open('wb') as out, (case / 'stderr.log').open('wb') as err:
            result = subprocess.run([str(binary), scenario], cwd=case, stdout=out, stderr=err, timeout=180)
        output = (case / 'stdout.log').read_text(encoding='utf-8').replace('\r\n', '\n')
        assert 'StudentTest9!' not in output
        results.append('\n' + scenario + '\n' + output)
        failed |= result.returncode != 0
        print(output, end='', flush=True)
        if result.returncode:
            print((case / 'stderr.log').read_text(encoding='utf-8', errors='replace'))
        after = snapshot(data)
        allowed = {'normal': {'reservations.txt', 'error.txt'}, 'changed': {'reservations.txt'},
                   'account-gone': {'students.txt'}}.get(scenario, set())
        assert {k: v for k, v in before.items() if k not in allowed} == {k: v for k, v in after.items() if k not in allowed}
        if scenario == 'normal':
            saved = (data / 'reservations.txt').read_text(encoding='utf-8').splitlines()
            assert saved[0] == '1|U1|1|20261020|0|0' and saved[3] == '4|U2|5|20261020|0|1'
            assert len((data / 'error.txt').read_text(encoding='utf-8').splitlines()) == 4
        results.append('PASS source files unchanged except authorized test mutations\n')
    assert protected == snapshot(root / 'data')
    results.append('PASS real development data hashes and modification times unchanged\n')
    output = ''.join(results)
    assert 'StudentTest9!' not in output
    (run / 'TEST_RESULTS.md').write_text('# P5.2 native GUI integration\n\n```text\n' + output + '```\n', encoding='utf-8', newline='\n')
    passed = sum(line.startswith('PASS ') for line in output.splitlines())
    failures = sum(line.startswith('FAIL ') for line in output.splitlines())
    print(f'{passed} passed, {failures} failed; report: {run / "TEST_RESULTS.md"}')
    return int(failed or failures > 0)


if __name__ == '__main__':
    raise SystemExit(main())
