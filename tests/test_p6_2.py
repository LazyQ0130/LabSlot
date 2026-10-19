"""All 63 cells, P4 equivalence, calendar boundaries, and real Win32 heatmap."""
import argparse
from datetime import date,timedelta
from p6_support import Suite,parse,row

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--cc',default='gcc');args=parser.parse_args()
    s=Suite('2',args.cc);binary=s.build('heatmap_probe')
    def call(case,day=20261020,who='student',env=None):
        out=s.call(binary,case,[who,day],env);return parse(out,'RESULT')[0][0],parse(out,'CELL'),out
    dates=[20261020,20261231,20270101,20240229,20260301]
    for index,value in enumerate(dates):
        ref=date(value//10000,value//100%100,value%100);monday=ref-timedelta(days=ref.weekday()); records='';expected=[];rid=1
        for d in range(7):
            target=monday+timedelta(days=d); stamp=int(target.strftime('%Y%m%d'))
            for slot in range(9):
                count=(d*9+slot)%41
                for no in range(1,count+1): records+=row(rid,student=f'T{no}',no=no,date=stamp,slot=slot);rid+=1
                records+=row(rid,date=stamp,slot=slot,status=0);rid+=1
                expected.append([stamp,slot,count,count*250])
        case=s.case('matrix-'+str(index),records,faults=(1,2,3))
        for who in ['student','admin']:
            code,cells,out=call(case,value,who)
            s.check('all 63 cells exact including calendar / cancelled / current faults '+who,code==0 and cells==expected)
        s.check('all palette boundaries exact',parse(out,'LEVEL')==[[n,-1 if n<0 or n>40 else 0 if n==0 else (n-1)//8] for n in range(-1,42)])
    empty=s.case('empty');code,cells,out=call(empty)
    s.check('entire empty week exact zeros',code==0 and len(cells)==63 and all(c[2:]==[0,0] for c in cells))
    # Allocation sweep uses the student path, no second P4 query in the probe.
    populated=case
    count=parse(call(populated)[2],'ALLOC')[0][0]
    for n in range(1,count+1): s.check('each allocation failure / output sentinel',call(populated,env={'LAB_TEST_FAIL_ALLOC':str(n)})[0] in (4,5))
    for who,want in [('none',2),('ghost',3)]:s.check('heatmap role/account '+who,call(empty,who=who)[0]==want)
    s.check('complete week lower boundary rejected',call(empty,10000101)[0]==9)
    s.check('invalid date rejected',call(empty,20260230)[0]==1)
    corrupt=s.case('corrupt',extra={'reservations.txt':'broken\n'});s.check('corrupt source refuses matrix',call(corrupt)[0]==4)
    recovery=s.case('recovery',extra={'maintenance.txt':'broken\n'});s.check('maintenance gate',call(recovery)[0]==8)
    s.check('clock failure',call(empty,env={'LAB_TEST_CLOCK_ERROR':'1'})[0]==7)
    gui=s.build('gui_heatmap_probe',True)
    tomorrow=date.today()+timedelta(days=1);stamp=int(tomorrow.strftime('%Y%m%d'));records='';rid=1
    for slot,count in enumerate([0,8,9,16,17,24,25,32,40]):
        for no in range(1,count+1):records+=row(rid,student=f'T{no}',no=no,date=stamp,slot=slot);rid+=1
    case=s.case('native-gui',records,gui=True)
    out=s.call(gui,case,[]);s.lines.extend(x for x in out.splitlines() if x.startswith(('PASS ','FAIL ')))
    s.finish()
if __name__=='__main__':main()
