"""Exact, explainable, read-only health analysis and native admin GUI."""
import argparse
import ctypes
from datetime import date,timedelta
from p6_support import Suite,parse,snapshot

def report(i,no,day,pending=False,student=None):
    return f'{i}|{student or "T"+str(i)}|{no}|{day:%Y%m%d}|合成故障线索|{0 if pending else 1}\n'

def sample(today):
    facts=[(1,a,p) for a,p in [(0,True),(6,False),(7,True),(29,False),(30,False),(70,False)]]
    facts += [(3,0,False),(3,0,False),(4,7,False),(6,30,True)]+[(5,0,False)]*20
    return facts,''.join(report(i+1,no,today-timedelta(days=age),pending) for i,(no,age,pending) in enumerate(facts))

def level(score):return 0 if score>=85 else 1 if score>=65 else 2 if score>=40 else 3

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--cc',default='gcc');args=parser.parse_args()
    s=Suite('4',args.cc);binary=s.build('health_probe',link_flags=['-Wl,--wrap=storage_load_reports,--wrap=storage_load_computers'])
    def call(case,who='admin',env=None,mode=None,readonly=True):
        out=s.call(binary,case,[who,*([mode] if mode else [])],env,readonly=readonly);return parse(out,'RESULT')[0][0],parse(out,'ROW'),out
    facts,reports=sample(date(2026,10,19));case=s.case('mixed',reports=reports,faults=(2,))
    expected=[];groups=[0]*4
    for no in range(1,41):
        ages=[(age,pending) for pc,age,pending in facts if pc==no]
        recent=sum(a<7 for a,p in ages);middle=sum(7<=a<30 for a,p in ages);older=sum(a>=30 for a,p in ages);pending=sum(p for a,p in ages)
        repeat=max(0,len(ages)-1)*2;penalty=recent*8+middle*3+older+pending*4+repeat;fault=65 if no==2 else 0;score=max(0,100-penalty-fault)
        warnings=(1 if fault else 0)|(2 if pending>=2 else 0)|(4 if recent+middle>=2 else 0)
        groups[level(score)]+=1;expected.append([no,int(no==2),score,level(score),recent,middle,older,pending,len(ages),penalty,fault,repeat,warnings])
    expected.sort(key=lambda r:(r[2],r[0]))
    code,rows,out=call(case)
    s.check('all 40 exact scores / factors / status / warning flags',code==0 and rows==expected)
    s.check('all grade totals exact',parse(out,'GROUP')==[groups] and sum(groups)==40)
    s.check('one computer and one complete report load',parse(out,'LOADS')==[[1,1]])
    s.check('every health grade boundary',parse(out,'LEVEL')==[[n,level(n) if 0<=n<=100 else -1] for n in range(-1,102)])
    s.check('same input deterministic',call(case)[2]==out)
    reverse=s.case('reversed',reports=''.join(reversed(reports.splitlines(keepends=True))),faults=(2,))
    s.check('source order cannot change repeated-report score or ranking',call(reverse)[1]==expected)
    allocations=parse(out,'ALLOC')[0][0]
    for n in range(1,allocations+1):s.check('every allocation failure preserves output',call(case,env={'LAB_TEST_FAIL_ALLOC':str(n)})[0] in (9,11))
    empty=s.case('empty');code,rows,_=call(empty)
    s.check('no reports normal exact 100 / stable ids',code==0 and rows==[[i,0,100,0,0,0,0,0,0,0,0,0,0] for i in range(1,41)])
    for age,want in [(0,92),(6,92),(7,97),(29,97),(30,99)]:
        case_age=s.case('age-'+str(age),reports=report(1,1,date(2026,10,19)-timedelta(days=age)))
        s.check('mutually exclusive age window '+str(age),call(case_age)[1][0][2]==want)
    s.check('pending modifier distinguished',call(s.case('pending',reports=report(1,1,date(2026,10,19),True)))[1][0][2]==88)
    for who,want in [('student',2),('none',2),('ghost',3)]:s.check('health real permission '+who,call(empty,who)[0]==want)
    s.check('student cannot view private report detail',call(s.case('privacy',reports=reports),'student',mode='detail')[0]==2)
    for name,extra,want in [('bad-report',{'error.txt':'broken\n'},9),('bad-computer',{'computers.txt':'1|0\n'},9),('recovery',{'maintenance.txt':'broken\n'},14)]:
        s.check('source or recovery rejection '+name,call(s.case(name,extra=extra))[0]==want)
    s.check('future report rejects entire trusted output',call(s.case('future',reports=reports+report(999,9,date(2026,10,20))))[0]==9)
    duplicate=report(1,1,date(2026,10,19));s.check('duplicate report id cannot be charged twice',call(s.case('duplicate',reports=duplicate+duplicate))[0]==9)
    s.check('clock error explicit',call(empty,env={'LAB_TEST_CLOCK_ERROR':'1'})[0]==13)
    s.check('leap-day age boundary',call(s.case('leap',reports=report(1,1,date(2024,2,29))),env={'LAB_TEST_NOW':'20240307,10,0,0'})[1][0][4:7]==[0,1,0])
    s.check('cross-year age uses day serial',call(s.case('year',reports=report(1,1,date(2026,12,31))),env={'LAB_TEST_NOW':'20270101,10,0,0'})[1][0][4:7]==[1,0,0])
    large_reports=''.join(report(i,1,date(2026,10,19)) for i in range(1,10001))
    s.check('10000 reports exact clamped score and penalty',call(s.case('maximum',reports=large_reports))[1][0]==[1,0,0,3,10000,0,0,0,10000,99998,0,19998,4])
    s.check('10001 reports refuse incomplete scores',call(s.case('capacity',reports=large_reports+report(10001,1,date(2026,10,19))))[0]==10)
    kernel=ctypes.WinDLL('kernel32',use_last_error=True);kernel.CreateFileW.argtypes=[ctypes.c_wchar_p,ctypes.c_uint32,ctypes.c_uint32,ctypes.c_void_p,ctypes.c_uint32,ctypes.c_uint32,ctypes.c_void_p];kernel.CreateFileW.restype=ctypes.c_void_p;kernel.CloseHandle.argtypes=[ctypes.c_void_p]
    locked=s.case('read-lock',reports=reports);before=snapshot(locked/'data');handle=kernel.CreateFileW(str(locked/'data/error.txt'),0x80000000,0,None,3,0,None);assert handle not in (None,ctypes.c_void_p(-1).value)
    try:code,_,_=call(locked,readonly=False)
    finally:kernel.CloseHandle(handle)
    s.check('real file sharing error rejects analysis / sources unchanged',code==9 and before==snapshot(locked/'data'))
    gui=s.build('gui_health_probe',True);_,gui_reports=sample(date.today());gui_case=s.case('native-gui',reports=gui_reports,faults=(2,),gui=True)
    out=s.call(gui,gui_case,[]);s.lines.extend(x for x in out.splitlines() if x.startswith(('PASS ','FAIL ')))
    out=s.call(gui,s.case('native-corrupt',gui=True,extra={'error.txt':'broken\n'}),['--corrupt']);s.lines.extend(x for x in out.splitlines() if x.startswith(('PASS ','FAIL ')))
    demo=s.build('gui_p6_demo_probe',True);demo_case=s.case('end-to-end',gui=True,extra={'students.txt':'','admins.txt':''});before=snapshot(demo_case/'data')
    out=s.call(demo,demo_case,[],readonly=False);s.lines.extend(x for x in out.splitlines() if x.startswith(('PASS ','FAIL ')))
    saved=(demo_case/'data/reservations.txt').read_text(encoding='utf-8').splitlines()
    s.check('end-to-end original-format two cancelled records retained',len(saved)==2 and all(line.split('|')[2]=='1' and line.split('|')[5]=='0' for line in saved))
    s.check('end-to-end actual processed report and normal device',len((demo_case/'data/error.txt').read_text(encoding='utf-8').splitlines())==1 and (demo_case/'data/error.txt').read_text(encoding='utf-8').splitlines()[0].split('|')[5]=='1' and (demo_case/'data/computers.txt').read_text(encoding='utf-8').splitlines()[0]=='1|0')
    s.check('end-to-end no implicit statistics export or active maintenance',snapshot(demo_case/'data')['count.txt']==before['count.txt'] and not (demo_case/'data/maintenance.txt').exists())
    s.finish()
if __name__=='__main__':main()
