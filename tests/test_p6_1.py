"""Exact deterministic recommendation tests and native Win32 message tests."""
import argparse
from p6_support import Suite, parse, row

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--cc',default='gcc');args=parser.parse_args()
    s=Suite('1',args.cc); binary=s.build('recommend_probe')
    def call(case,start=0,length=1,pref=0,who='U1',date=20261020,env=None,mode='recommend'):
        out=s.call(binary,case,[mode,who,date,start,length,pref],env)
        return parse(out,'RESULT')[0][0],parse(out,'ROW'),out
    empty=s.case('empty')
    code,rows,out=call(empty)
    s.check('empty exact top three / full normalized scores',code==0 and rows==[[i,10000,10000,10000,10000,9] for i in [1,2,3]])
    allocations=parse(out,'ALLOC')[0][0]
    s.check('same data deterministic byte-identical output',call(empty)[2]==out)
    for n in range(1,allocations+1):
        code,_,_=call(empty,env={'LAB_TEST_FAIL_ALLOC':str(n)})
        s.check('every allocation failure rejects / sentinel preserved',code in (12,14))
    fixture=row(no=1,slot=0)+row(2,no=2,slot=2)+row(3,no=9,slot=8)+row(4,no=3,slot=0,status=0)
    case=s.case('weights',fixture,faults=(4,))
    # Independent Python scoring oracle over all 40 candidates.
    def expected(pref):
        weights=[[70,15,15],[20,70,10],[20,10,70]][pref]; result=[]
        occupied={1:{0},2:{2},9:{8}}
        for no in range(1,41):
            busy=occupied.get(no,set())
            if no==4 or 0 in busy:continue
            end=0
            while end<9 and end not in busy:end+=1
            c=end*10000//9;p=(4-(no-1)//8)*2500;l=(9-len(busy))*10000//9
            result.append([no,(c*weights[0]+p*weights[1]+l*weights[2])//100,c,p,l,end])
        return sorted(result,key=lambda r:(-r[1],r[0]))[:3]
    for pref in range(3):
        code,actual,_=call(case,pref=pref)
        s.check('exact candidate factor scores / total / stable order '+str(pref),code==0 and actual==expected(pref))
    # Force a front-row tradeoff against empty back row: preference must change winner.
    fixture=''.join(row(i+1,student=f"S{i}",no=i+1,slot=4) for i in range(8))
    trade=s.case('tradeoff',fixture)
    s.check('preference changes actual ordering',call(trade,pref=0)[1][0][0]==9 and call(trade,pref=1)[1][0][0]==1 and call(trade,pref=2)[1][0][0]==9)
    mask=s.call(binary,case,['mask','U1',20261020,0,1,0])
    masks=parse(mask,'MASK')
    s.check('all 40 exact nine-bit masks / actual loads',len(masks)==40 and all(v==[0 if n==4 else 511-sum(1<<i for i in {1:{0},2:{2},9:{8}}.get(n,set())),len({1:{0},2:{2},9:{8}}.get(n,set()))] for n,v in enumerate(masks,1)))
    own=s.case('own-conflict',row(student='U1',no=40,slot=1))
    s.check('own conflict excludes whole request',call(own,length=2)[0]==6)
    s.check('own conflict clears every computer bit',all(not (x[0]&2) for x in parse(s.call(binary,own,['mask','U1',20261020,0,1,0]),'MASK')))
    for start,length,want in [(8,1,0),(7,2,0),(6,3,0),(8,2,1),(7,3,1),(-1,1,1),(0,0,1),(0,4,1)]:
        s.check('range boundary '+str((start,length)),call(empty,start,length)[0]==want)
    for who,want in [('admin',2),('none',2),('ghost',3)]:s.check('business role/account gate '+who,call(empty,who=who)[0]==want)
    for date,want in [(20261018,10),(20261026,11),(20260230,1)]:s.check('date/window validation '+str(date),call(empty,date=date)[0]==want)
    s.check('start equality rejected',call(empty,start=2,date=20261019,env={'LAB_TEST_NOW':'20261019,10,0,0'})[0]==10)
    s.check('today future slots available',call(empty,start=3,date=20261019)[0]==0)
    s.check('clock error is explicit',call(empty,env={'LAB_TEST_CLOCK_ERROR':'1'})[0]==16)
    s.check('cross year uses calendar window',call(empty,date=20270101,env={'LAB_TEST_NOW':'20261231,10,0,0'})[0]==0)
    stale=s.case('stale')
    stale_output=s.call(binary,stale,['stale','U1',20261020,0,1,0],readonly=False)
    s.check('stale recommendation final create rejects occupied seat',parse(stale_output,'RESULT')==[[5]] and (stale/'data/reservations.txt').read_text().splitlines()==['1|U2|1|20261020|0|1'])
    large=s.case('maximum-source',''.join(row(i,student='U2',status=0) for i in range(1,100001)))
    s.check('100000 records exact output preserved',call(large)[1]==[[i,10000,10000,10000,10000,9] for i in [1,2,3]])
    faults=s.case('all-fault',faults=range(1,41));s.check('no candidates is legal empty result',call(faults)[0:2]==(0,[]))
    corrupt=s.case('corrupt',extra={'reservations.txt':'broken\n'});s.check('corrupt input rejects full analysis',call(corrupt)[0]==12)
    recovery=s.case('recovery',extra={'maintenance.txt':'P3|1|1|1|0|20261019|10|0|0\n'});s.check('P3 recovery distinguished',call(recovery)[0]==17)
    gui=s.build('gui_recommend_probe',True);gui_case=s.case('native-gui',gui=True)
    output=s.call(gui,gui_case,[])
    s.lines.extend(x for x in output.splitlines() if x.startswith(('PASS ','FAIL ')))
    s.finish()
if __name__=='__main__':main()
