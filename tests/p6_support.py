"""Isolated P6 helpers: preserve every run, no deletion, no credentials in logs."""
from pathlib import Path
import hashlib
import os
import re
import subprocess
import time
import uuid

ROOT = Path(__file__).resolve().parents[1]
def snapshot(directory):
    return {p.name: (hashlib.sha256(p.read_bytes()).hexdigest(), p.stat().st_mtime_ns)
            for p in directory.iterdir() if p.is_file()}

class Suite:
    def __init__(self, stage, cc):
        self.stage, self.cc, self.lines = stage, cc, []
        self.run = ROOT/'tests/runs'/('p6-'+stage+'-'+time.strftime('%Y%m%d-%H%M%S')+'-'+uuid.uuid4().hex[:8])
        self.run.mkdir(parents=True)
        self.protected = snapshot(ROOT/'data')
        self.flags = [cc, '-std=c11', '-Wall', '-Wextra', '-Wpedantic', '-Werror', '-I'+str(ROOT/'include')]
    def check(self, name, condition):
        self.lines.append(('PASS ' if condition else 'FAIL ')+name)
        if not condition:
            self.finish(False)
            raise AssertionError(name)
    def build(self, probe, gui=False, link_flags=()):
        sources = sorted(p for p in (ROOT/'src').glob('*.c') if p.name not in ('main.c','ui.c'))
        flags = self.flags[:]
        if gui:
            sources += sorted(p for p in (ROOT/'src/gui').glob('*.c') if p.name!='gui_main.c')
            extra = [str(ROOT/'src/gui/gui_resource.o'),'-lcomctl32','-lgdi32','-luser32']
        else:
            flags += ['-include',str(ROOT/'tests/io_faults.h')]
            extra = ['-Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc']
        binary=self.run/(probe+'.exe')
        subprocess.run([*flags,*map(str,sources),str(ROOT/'tests'/(probe+'.c')),*extra,*link_flags,'-o',str(binary)],check=True)
        return binary
    def case(self, name, reservations='', faults=(), gui=False, reports='', extra=None):
        directory=self.run/name; data=directory/'data'; data.mkdir(parents=True)
        secret='FinalTest9!' if gui else 'P2Synthetic!'
        files={'students.txt': ''.join(f'{sid}|合成学生|{secret}|0\n' for sid in ['U1','U2','U3']),
               'admins.txt':f'{"A1" if gui else "Admin"}|合成管理员|{secret}\n',
               'computers.txt':''.join(f'{i}|{int(i in faults)}\n' for i in range(1,41)),
               'reservations.txt':reservations,'error.txt':reports,'count.txt':''}
        files.update(extra or {})
        for k,v in files.items(): (data/k).write_text(v,encoding='utf-8',newline='\n')
        return directory
    def call(self, binary, directory, args, env=None, readonly=True):
        before=snapshot(directory/'data') if readonly else None
        variables={k:v for k,v in os.environ.items() if not k.startswith('LAB_TEST_')};variables.update(env or {})
        result=subprocess.run([str(binary),*map(str,args)],cwd=directory,env=variables,capture_output=True,timeout=180)
        output=result.stdout.decode('utf-8').replace('\r\n','\n')
        assert all(secret not in output+result.stderr.decode('utf-8',errors='replace') for secret in ['FinalTest9!','P2Synthetic!'])
        index=len(list(directory.glob('call-*-stdout.log')))+1
        (directory/f'call-{index:03d}-stdout.log').write_bytes(result.stdout)
        (directory/f'call-{index:03d}-stderr.log').write_bytes(result.stderr)
        self.check('probe exits successfully',result.returncode==0)
        if readonly: self.check('analysis preserves all source hashes / mtimes',before==snapshot(directory/'data'))
        return output
    def finish(self, verify=True):
        if verify: self.check('real development data hashes / mtimes unchanged',self.protected==snapshot(ROOT/'data'))
        output='\n'.join(self.lines)+'\n'
        passed=sum(s.startswith('PASS ') for s in self.lines); failed=sum(s.startswith('FAIL ') for s in self.lines)
        output+=f'{passed} passed, {failed} failed\n'
        (self.run/'TEST_RESULTS.md').write_text(output,encoding='utf-8')
        print(output,end='');print('Report:',self.run/'TEST_RESULTS.md')
        return passed

def parse(output,key):
    return [list(map(int,v.split(','))) for v in re.findall(r'^'+key+r'=(.*)$',output,re.M)]

def row(i=1, student='U2', no=1, date=20261020, slot=0, status=1):
    return f'{i}|{student}|{no}|{date}|{slot}|{status}\n'
