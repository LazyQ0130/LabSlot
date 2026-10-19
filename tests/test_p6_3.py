"""Atomic complete snapshots: failure leaves original bytes and all outputs unchanged."""
import argparse
import ctypes
from p6_support import Suite,parse,row,snapshot

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--cc',default='gcc');args=parser.parse_args()
    s=Suite('3',args.cc);binary=s.build('batch_probe',link_flags=['-Wl,--wrap=storage_load_reservations,--wrap=storage_save_reservations'])
    def call(case,start=0,length=2,no=1,day=20261020,mode='create',who='U1',env=None):
        before=snapshot(case/'data')
        out=s.call(binary,case,[mode,who,day,start,length,no],env,readonly=mode in ['check','list']);code=parse(out,'RESULT')[0][0]
        if mode=='create' and code!=0:s.check('rejected batch preserves every original source',all(snapshot(case/'data')[k]==v for k,v in before.items()))
        return code,parse(out,'ROW'),out
    for length in [1,2,3]:
        case=s.case('success-'+str(length));before=snapshot(case/'data')
        s.check('read-only precheck '+str(length),call(case,length=length,mode='check')[0]==0)
        code,rows,out=call(case,length=length)
        s.check('exact complete one-hour group '+str(length),code==0 and rows==[[i+1,1,20261020,i,1] for i in range(length)] and parse(out,'READS')==[[2]] and parse(out,'LOADS')==[[1]] and parse(out,'SAVES')==[[1]])
        s.check('restart reads every record',call(case,mode='list')[1]==rows)
        s.check('only reservation target changes',all(snapshot(case/'data')[k]==v for k,v in before.items() if k!='reservations.txt'))
    success=case;allocations=parse(out,'ALLOC')[0][0]
    call(success,day=2,mode='cancel');s.check('single cancellation preserves other group slots',call(success,mode='list')[1]==[[1,1,20261020,0,1],[2,1,20261020,1,0],[3,1,20261020,2,1]])
    for n in range(1,allocations+1):
        case=s.case('memory-'+str(n));before=snapshot(case/'data');code,_,_=call(case,length=3,env={'LAB_TEST_FAIL_ALLOC':str(n)})
        s.check('each allocation fails safely',code in (12,14) and before==snapshot(case/'data'))
    for slot in [0,1,2]:
        for who,no,want in [('U2',1,5),('U1',2,6)]:
            case=s.case('conflict-'+str(slot)+who,row(student=who,no=no,slot=slot));before=(case/'data/reservations.txt').read_bytes()
            s.check('any covered seat/student conflict rejects entire group',call(case,length=3)[0]==want and (case/'data/reservations.txt').read_bytes()==before)
    for start,length,want in [(7,2,0),(6,3,0),(8,2,1),(7,3,1),(-1,2,1),(0,0,1),(0,4,1)]:
        case=s.case('bounds-'+str(start)+'-'+str(length));s.check('17:00 boundary '+str((start,length)),call(case,start,length)[0]==want)
    for who,want in [('none',2),('admin',2),('ghost',3)]:s.check('batch session gate '+who,call(s.case('role-'+who),who=who)[0]==want)
    for day,want in [(20261018,10),(20261026,11),(20260230,1)]:s.check('batch date/window gate',call(s.case('date-'+str(day)),day=day)[0]==want)
    for no in [-1,41]:s.check('batch invalid computer',call(s.case('pc-'+str(no)),no=no)[0]==1)
    s.check('historical cancelled maximum not reused',call(s.case('id-history',row(9,status=0)+row(2,no=2,date=20261021,slot=3)),length=3)[1]==[[10,1,20261020,0,1],[11,1,20261020,1,1],[12,1,20261020,2,1]])
    s.check('fault device refused',call(s.case('fault',faults=(1,)))[0]==4)
    s.check('commit clock recheck rejects start equality',call(s.case('clock-boundary'),start=3,env={'LAB_TEST_LATER':'20261020,11,0,0'})[0]==10)
    s.check('clock error explicit',call(s.case('clock-error'),env={'LAB_TEST_CLOCK_ERROR':'1'})[0]==16)
    s.check('cross-year valid window',call(s.case('new-year'),day=20270101,env={'LAB_TEST_NOW':'20261231,10,0,0'})[0]==0)
    s.check('P3 maintenance gate',call(s.case('maintenance',extra={'maintenance.txt':'broken\n'}))[0]==17)
    s.check('bad reservation input',call(s.case('corrupt',extra={'reservations.txt':'broken\n'}))[0]==12)
    for maximum,length,want in [(2147483646,2,15),(2147483645,3,15),(2147483645,2,0)]:
        case=s.case('id-'+str(maximum)+'-'+str(length),row(maximum,status=0));code,rows,_=call(case,length=length)
        s.check('INT_MAX allocation boundary',code==want and (code!=0 or [r[0] for r in rows]==[maximum+1,maximum+2]))
    for count,length,want in [(100000,2,13),(99999,2,13),(99998,2,0)]:
        case=s.case('capacity-'+str(count),''.join(row(i,status=0) for i in range(1,count+1)));code,rows,_=call(case,length=length)
        s.check('exact capacity boundary',code==want and (code!=0 or [r[0] for r in rows]==[99999,100000]))
    for mode in ['write','flush','close','exclusive']:
        case=s.case('io-'+mode,row(8,status=0),extra={'reservations.txt.tmp':'owned draft\n'} if mode=='exclusive' else None);before=snapshot(case/'data')
        code,_,_=call(case,length=3,env={'LAB_TEST_IO':mode} if mode!='exclusive' else None)
        after=snapshot(case/'data');s.check('save failure is all-or-none '+mode,code==12 and all(after[k]==v for k,v in before.items()))
    kernel=ctypes.WinDLL('kernel32',use_last_error=True)
    kernel.CreateFileW.argtypes=[ctypes.c_wchar_p,ctypes.c_uint32,ctypes.c_uint32,ctypes.c_void_p,ctypes.c_uint32,ctypes.c_uint32,ctypes.c_void_p];kernel.CreateFileW.restype=ctypes.c_void_p
    kernel.CloseHandle.argtypes=[ctypes.c_void_p]
    case=s.case('replacement-lock',row(9,status=0));before=snapshot(case/'data')
    handle=kernel.CreateFileW(str(case/'data/reservations.txt'),0x80000000,3,None,3,0,None);assert handle not in (None,ctypes.c_void_p(-1).value)
    try:code,_,_=call(case,length=3)
    finally:kernel.CloseHandle(handle)
    s.check('real Windows replacement lock preserves original',code==12 and all(snapshot(case/'data')[k]==v for k,v in before.items()) and (case/'data/reservations.txt.tmp').exists())
    gui=s.build('gui_batch_probe',True,['-Wl,--wrap=booking_availability']);case=s.case('native-gui',gui=True);before=snapshot(case/'data')
    out=s.call(gui,case,[],readonly=False);s.lines.extend(x for x in out.splitlines() if x.startswith(('PASS ','FAIL ')))
    saved=(case/'data/reservations.txt').read_text().splitlines();s.check('GUI persists all five original-format rows',len(saved)==5 and all(len(line.split('|'))==6 for line in saved))
    s.check('GUI only reservation file changes',all(snapshot(case/'data')[k]==v for k,v in before.items() if k!='reservations.txt'))
    s.finish()
if __name__=='__main__':main()
