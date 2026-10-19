"""P5.3 native Windows controls/messages, isolated files and actual maintenance recovery."""
import argparse
import hashlib
from pathlib import Path
import subprocess
import time
import uuid


def snapshot(directory):
    return {p.name: (hashlib.sha256(p.read_bytes()).hexdigest(), p.stat().st_mtime_ns)
            for p in directory.iterdir() if p.is_file()}


def fixtures():
    return {
        'students.txt': 'U1|合成学生甲|AdminTest9!|999\nU2|合成学生乙|AdminTest9!|0\n',
        'admins.txt': 'A1|合成管理员|AdminTest9!\n',
        'computers.txt': ''.join(f'{i}|{int(i == 2)}\n' for i in range(1, 41)),
        'reservations.txt': ('1|U1|1|20261019|2|1\n2|U2|1|20261019|3|1\n'
                             '3|U1|1|20261020|0|1\n4|U2|2|20261019|2|1\n'
                             '5|U2|3|20261019|0|1\n6|U1|4|20261018|0|1\n'
                             '7|U2|4|20261019|4|0\n8|U1|5|20261025|8|1\n'
                             '9|U2|6|20261020|1|1\n10|U1|7|20261018|1|0\n'
                             '11|U1|3|20261019|4|1\n'),
        'error.txt': ('1|U1|3|20261019|显示器闪烁，待确认|0\n'
                      '2|U2|2|20261019|设备维修后请检查|0\n'
                      '3|U1|4|20261018|键盘偶发无响应|0\n'
                      '4|U2|5|20261018|已处理记录|1\n'),
        'count.txt': '',
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--cc', default='gcc')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    run = root / 'tests/runs' / ('p5-3-' + time.strftime('%Y%m%d-%H%M%S') + '-' + uuid.uuid4().hex[:8])
    run.mkdir(parents=True)
    protected = snapshot(root / 'data')
    flags = [args.cc, '-std=c11', '-Wall', '-Wextra', '-Wpedantic', '-Werror', '-I' + str(root / 'include')]
    clock_object = run / 'utils-test-clock.o'
    subprocess.run([*flags, '-Dutils_local_time=gui_real_local_time', '-c', str(root / 'src/utils.c'), '-o', str(clock_object)], check=True)
    fault_object = run / 'fault-test-query.o'
    subprocess.run([*flags, '-Dfault_query_reports=gui_real_fault_query_reports', '-c', str(root / 'src/fault.c'), '-o', str(fault_object)], check=True)
    sources = sorted(p for p in (root / 'src').glob('*.c') if p.name not in ('main.c', 'ui.c', 'utils.c', 'fault.c'))
    sources += sorted(p for p in (root / 'src/gui').glob('*.c') if p.name != 'gui_main.c')
    binary = run / 'gui-admin-probe.exe'
    subprocess.run([*flags, *map(str, sources), str(root / 'tests/gui_admin_probe.c'), str(clock_object), str(fault_object),
                    str(root / 'src/gui/gui_resource.o'), '-lcomctl32', '-lgdi32', '-luser32', '-o', str(binary)], check=True)
    results = []
    failed = False
    scenarios = ['normal', 'student', 'empty', 'corrupt', 'corrupt-reports', 'save-failure',
                 'changed', 'refresh-failure', 'recovery', 'large', 'preview-many', 'account-gone']
    for scenario in scenarios:
        print('Running', scenario, flush=True)
        case = run / scenario
        data = case / 'data'
        data.mkdir(parents=True)
        files = fixtures()
        if scenario == 'empty':
            files['reservations.txt'] = files['error.txt'] = ''
        elif scenario == 'corrupt':
            files['reservations.txt'] = 'invalid fixture\n'
        elif scenario == 'corrupt-reports':
            files['error.txt'] = 'invalid fixture\n'
        elif scenario == 'save-failure':
            files['count.txt.tmp'] = 'test owns this exclusive temporary path\n'
        elif scenario == 'large':
            files['reservations.txt'] = ''.join(f'{i}|U1|1|20261018|0|0\n' for i in range(1, 100001))
            files['error.txt'] = ''.join(f'{i}|U1|1|20261018|历史报告|1\n' for i in range(1, 10001))
        elif scenario == 'preview-many':
            from datetime import date, timedelta
            files['reservations.txt'] = ''.join(
                f'{i + 1}|U1|1|{(date(2026, 10, 20) + timedelta(days=i)):%Y%m%d}|0|1\n'
                for i in range(41))
        for name, content in files.items():
            (data / name).write_text(content, encoding='utf-8', newline='\n')
        before = snapshot(data)
        modes = [scenario, 'recover-restart'] if scenario == 'recovery' else [scenario]
        for mode in modes:
            with (case / (mode + '-stdout.log')).open('wb') as out, (case / (mode + '-stderr.log')).open('wb') as err:
                result = subprocess.run([str(binary), mode], cwd=case, stdout=out, stderr=err, timeout=240)
            output = (case / (mode + '-stdout.log')).read_text(encoding='utf-8').replace('\r\n', '\n')
            assert 'AdminTest9!' not in output
            failed |= result.returncode != 0
            print(output, end='', flush=True)
            results.append('\n' + mode + '\n' + output)
            if result.returncode:
                print((case / (mode + '-stderr.log')).read_text(encoding='utf-8', errors='replace'))
        allowed = {
            'normal': {'computers.txt', 'reservations.txt', 'error.txt', 'count.txt', 'maintenance.done.txt'},
            'changed': {'computers.txt', 'reservations.txt', 'error.txt', 'maintenance.done.txt'},
            'refresh-failure': {'computers.txt', 'reservations.txt', 'error.txt', 'maintenance.done.txt'},
            'recovery': {'computers.txt', 'reservations.txt', 'maintenance.done.txt'},
            'account-gone': {'admins.txt'},
        }.get(scenario, set())
        after = snapshot(data)
        assert {k: v for k, v in before.items() if k not in allowed} == {k: v for k, v in after.items() if k not in allowed}
        results.append('PASS fixture hashes and modification times unchanged except authorized mutations\n')
    assert protected == snapshot(root / 'data')
    results.append('PASS real development data hashes and modification times unchanged\n')
    output = ''.join(results)
    (run / 'TEST_RESULTS.md').write_text('# P5.3 native GUI integration\n\n```text\n' + output + '```\n', encoding='utf-8', newline='\n')
    passed = sum(line.startswith('PASS ') for line in output.splitlines())
    failures = sum(line.startswith('FAIL ') for line in output.splitlines())
    print(f'{passed} passed, {failures} failed; report: {run / "TEST_RESULTS.md"}')
    return int(failed or failures > 0)


if __name__ == '__main__':
    raise SystemExit(main())
