"""P4 隔离统计测试：固定时钟、真实快照I/O/替换失败、P3恢复门禁与逐次分配失败。"""
import argparse
import datetime
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import traceback
import uuid

OK, INVALID, UNAUTHORIZED, MISSING, STORAGE, MEMORY, CAPACITY, CLOCK, RECOVERY, DATE_RANGE = range(10)
PASSWORD = "P4Synthetic!"  # 合成凭据仅用于隔离账户，不写入日志/报告。


def lines(*values):
    """物理行转测试输入；不记录输入。"""
    return b"\n".join(v if isinstance(v, bytes) else str(v).encode() for v in values) + b"\n"


def reservation(i, student="U1", no=1, date=20261020, slot=3, status=1):
    """返回单条预约事实。"""
    return lines(f"{i}|{student}|{no}|{date}|{slot}|{status}")


def main():
    """构建、执行隔离测试并输出无凭据报告，失败返回非零。"""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", default="smart-lab.exe")
    parser.add_argument("--cc", default="gcc")
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    cc = shutil.which(args.cc)
    executable = Path(args.executable).resolve()
    if not cc or not executable.is_file():
        parser.error("需要 GCC 和正式可执行程序")
    run_root = root / "tests/runs" / ("p4-" + datetime.datetime.now().strftime("%Y%m%d-%H%M%S-") + uuid.uuid4().hex[:8])
    run_root.mkdir(parents=True)
    original = {p.name: p.read_bytes() for p in (root / "data").iterdir() if p.is_file()}
    probe, ui = run_root / "stats-probe.exe", run_root / "fixed-ui.exe"
    sources = [str(root / "src" / f"{n}.c") for n in ["auth", "storage", "utils", "lab", "booking", "fault", "admin", "stats"]]
    flags = [cc, "-std=c11", "-Wall", "-Wextra", "-Wpedantic", "-Werror", "-I", str(root / "include")]
    builds = [flags + ["-include", str(root / "tests/io_faults.h"), str(root / "tests/stats_probe.c"), *sources,
                       "-Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc", "-o", str(probe)],
              flags + [str(root / "tests/booking_ui_main.c"), str(root / "src/ui.c"), *sources, "-o", str(ui)]]
    for i, command in enumerate(builds):
        built = subprocess.run(command, capture_output=True, timeout=90)
        (run_root / f"build-{i}.log").write_bytes(built.stdout + built.stderr)
        if built.returncode:
            print("Driver build failed", i, run_root)
            return 1
    results = []

    def case(name, bookings=b"", counts=b"", faults=()):
        """新建完整隔离数据，不清理现场。"""
        d = run_root / name
        data = d / "data"
        data.mkdir(parents=True)
        (data / "computers.txt").write_bytes(b"".join(lines(f"{n}|{int(n in faults)}") for n in range(1, 41)))
        (data / "students.txt").write_bytes(b"".join(lines(f"{s}|{name}|{PASSWORD}|12") for s, name in [("U1", "张三"), ("U2", "李四"), ("U3", "王五")]))
        (data / "admins.txt").write_bytes(lines(f"Admin|管理员|{PASSWORD}"))
        (data / "reservations.txt").write_bytes(bookings)
        (data / "error.txt").write_bytes(b"")
        (data / "count.txt").write_bytes(counts)
        return d

    def run(d, command, data=b"", env=None, exit_code=0, allow=()):
        """运行真实进程，凭据检查后才保存 stdout/stderr，断言保护账户/累计时间。"""
        environment = {k: v for k, v in os.environ.items() if not k.startswith("LAB_TEST_")}
        environment.update(env or {})
        protected = {n: ((d / "data" / n).read_bytes(), (d / "data" / n).stat().st_mtime_ns) for n in ["students.txt", "admins.txt", "count.txt", "reservations.txt", "computers.txt", "error.txt"] if n not in allow and (d / "data" / n).is_file()}
        r = subprocess.run(command, input=data, cwd=d, env=environment, capture_output=True, timeout=45)
        assert PASSWORD.encode() not in r.stdout + r.stderr
        number = len(list(d.glob("stdout-*.txt"))) + 1
        (d / f"stdout-{number}.txt").write_bytes(r.stdout)
        (d / f"stderr-{number}.txt").write_bytes(r.stderr)
        assert r.returncode == exit_code
        assert all(((d / "data" / n).read_bytes(),(d / "data" / n).stat().st_mtime_ns)==content for n, content in protected.items())
        return r

    def call(d, mode, *values, who="admin", env=None, exit_code=0):
        """调用公开 API 驱动，输出只含业务事实。"""
        allow = ("count.txt",) if mode == "export" else (("computers.txt","reservations.txt","error.txt") if mode in ["recover","crash-maintenance"] else (("reservations.txt",) if mode=="cancel" else ()))
        r = run(d, [str(probe), mode, who, *map(str, values)], env=env, exit_code=exit_code, allow=allow)
        parsed = {"stdout": r.stdout, "stderr": r.stderr}
        for key, value in re.findall(rb"^([A-Z]+)=(.*)$", r.stdout, re.M):
            key, value = key.decode(), value.rstrip(b"\r").decode()
            if key in ["DAY", "WEEK", "STUDENT"]:
                parsed.setdefault(key, []).append(value.split(","))
            else:
                parsed[key] = int(value) if re.fullmatch(r"-?\d+", value) else value
        return parsed

    def check(name, function):
        """记录单个验收，异常只报告类型和代码位置，防止泄露账户。"""
        try:
            function()
            results.append({"name": name, "passed": True, "detail": ""})
            print("PASS", name)
        except Exception as error:
            detail = f"{type(error).__name__} at line {traceback.extract_tb(error.__traceback__)[-1].lineno}"
            results.append({"name": name, "passed": False, "detail": detail})
            print("FAIL", name, detail)


    def day(q, index=0):
        """解析测试投影。返回：日期/数量/人数/小时/完成/利用率/九时段。"""
        row=q["DAY"][index]
        return [int(x) for x in row[:5]]+[float(row[5])]+[int(x) for x in row[6:]]

    def count_row(date, total=0, students=0, slots=None):
        """生成合法快照测试行。"""
        slots=slots if slots is not None else [total]+[0]*8
        return lines("|".join(map(str,[date,total,students,total,*slots])))

    def daily():
        b=(reservation(1,date=20261019,slot=0)+reservation(2,date=20261019,slot=1)+
           reservation(3,"U2",2,date=20261019,slot=1)+reservation(4,date=20261019,slot=3)+
           reservation(5,date=20261019,slot=2,status=0)+reservation(6,date=20261020))
        d=case("daily",b)
        q=call(d,"daily",20261019)
        row=day(q)
        assert list(map(float,q["RATES"].split(",")))==[2.5,5.0,0.0,2.5,0.0,0.0,0.0,0.0,0.0]
        assert row[:5]==[20261019,4,2,4,3]
        assert abs(row[5]-4/360*100)<1e-7 and row[6:]==[1,2,0,1,0,0,0,0,0]
        assert day(call(d,"daily",20261021))[1:6]==[0,0,0,0,0.0]
        assert (d / "data/count.txt").read_bytes()==b""
    check("日报有效/取消/分时段/人数去重/理论利用率/已完成/只读",daily)

    def weekly():
        b=b"";identifier=1
        for offset in range(7):
            date=int((datetime.date(2026,10,19)+datetime.timedelta(days=offset)).strftime("%Y%m%d"))
            b+=reservation(identifier,date=date);identifier+=1
        b+=reservation(identifier,"U2",2,date=20261020)
        d=case("weekly",b)
        q=call(d,"week",20261022)
        w=q["WEEK"][0]
        assert list(map(int,w[:4]))==[20261019,20261025,8,2]
        assert abs(float(w[4])-8/7)<1e-7 and abs(float(w[5])-8/2520*100)<1e-7
        assert [day(q,i)[2] for i in range(7)]==[1,2,1,1,1,1,1]
        assert sum(day(q,i)[2] for i in range(7))!=2
        assert [day(q,i)[0] for i in range(7)]==[20261019+i for i in range(7)]
    check("整周一次聚合/跨天去重/日与周口径/日均/理论利用率",weekly)

    for text in ["2026-11-01","2027-01-01","2024-02-29","2000-02-29","2100-03-01","1000-01-07","9999-12-26"]:
        def calendar(text=text):
            date=datetime.date.fromisoformat(text)
            monday=date-datetime.timedelta(days=date.weekday())
            sunday=monday+datetime.timedelta(days=6)
            d=case("calendar-"+text)
            q=call(d,"week",int(date.strftime("%Y%m%d")))
            assert list(map(int,q["WEEK"][0][:4]))==[int(monday.strftime("%Y%m%d")),int(sunday.strftime("%Y%m%d")),0,0]
            assert [day(q,i)[0] for i in range(7)]==[int((monday+datetime.timedelta(days=i)).strftime("%Y%m%d")) for i in range(7)]
        check("空周/跨月跨年闰年日历 "+text,calendar)

    for date in [20260230,20250229,21000229,0,99991301,-1,2147483647]:
        def invalid_date(date=date):
            d=case("date-"+str(date))
            assert call(d,"daily",date)["RESULT"]==INVALID
            assert call(d,"week",date)["RESULT"]==INVALID
            assert call(d,"export",date)["RESULT"]==INVALID
        check("非法日期拒绝 "+str(date),invalid_date)
    for date in [10000101,99991231]:
        def range_edge(date=date):
            d=case("range-"+str(date))
            assert call(d,"daily",date)["RESULT"]==OK
            assert call(d,"week",date)["RESULT"]==DATE_RANGE
        check("合法日期整周越界明确拒绝 "+str(date),range_edge)

    for now,finished in [("20261019,8,59,59",0),("20261019,9,0,0",1),("20261019,10,0,0",2),
                         ("20261019,10,30,0",2),("20261019,11,0,0",3),("20261019,17,0,0",4),
                         ("20261018,23,59,59",0),("20261020,0,0,0",4)]:
        def boundary(now=now,finished=finished):
            b=b"".join(reservation(i+1,date=20261019,slot=slot) for i,slot in enumerate([0,1,2,8]))
            b+=reservation(5,date=20261018,status=0)+reservation(6,"U2",2,date=20261018)
            d=case("boundary-"+now.replace(",","-"),b)
            q=call(d,"student",who="U1",env={"LAB_TEST_NOW":now})
            assert list(map(int,q["STUDENT"][0]))==[finished,4,finished,4-finished,1]
            assert q["IDENTITY"]=="U1,张三"
            assert day(call(d,"daily",20261019,env={"LAB_TEST_NOW":now}))[4]==finished
        check("固定时钟时段结束边界 "+now,boundary)

    def student_cache():
        d=case("student-cache",reservation(1,date=20261018)+reservation(2,date=20261020)+reservation(3,"U2",2,date=20261017))
        p=d / "data/students.txt"
        p.write_bytes(p.read_bytes().replace(b"|12\n",b"|2147483647\n"))
        for unused in range(2):
            q=call(d,"student",who="U1")
            assert list(map(int,q["STUDENT"][0]))==[1,2,1,1,0]
        assert list(map(int,call(d,"student",who="U2")["STUDENT"][0]))==[1,1,1,0,0]
        assert list(map(int,call(d,"student",who="U3")["STUDENT"][0]))==[0,0,0,0,0]
    check("重启/动态累计/忽略过时INT_MAX缓存/本人隔离/空学生",student_cache)

    for mode,who in [("daily","U1"),("week","U1"),("export","U1"),("student","admin"),
                     ("daily","none"),("week","none"),("export","none"),("student","none")]:
        def unauthorized(mode=mode,who=who):
            d=case("role-"+mode+"-"+who)
            assert call(d,mode,20261019,who=who)["RESULT"]==UNAUTHORIZED
        check("统计业务权限 "+mode+"/"+who,unauthorized)
    for who,mode in [("ghostadmin","daily"),("ghostadmin","week"),("ghostadmin","export"),("ghost","student")]:
        def missing(who=who,mode=mode):
            d=case("missing-"+mode)
            assert call(d,mode,20261019,who=who)["RESULT"]==MISSING
        check("真实账户存在性 "+mode,missing)
    def nulls():
        assert call(case("null-outputs"),"null-output")["RESULT"]==OK
    check("空输出参数安全拒绝",nulls)

    for mode in ["daily","week","student","export"]:
        def bad_clock(mode=mode):
            d=case("clock-"+mode)
            who="U1" if mode=="student" else "admin"
            assert call(d,mode,20261019,who=who,env={"LAB_TEST_CLOCK_ERROR":"1"})["RESULT"]==CLOCK
            assert call(d,mode,20261019,who=who,env={"LAB_TEST_NOW":"20260230,10,0,0"})["RESULT"]==CLOCK
        check("统计无效/失败时钟不发布结果 "+mode,bad_clock)

    def export_update():
        d=case("export-update",reservation(1,date=20261020))
        assert call(d,"export",20261020)["RESULT"]==OK
        first=(d / "data/count.txt").read_bytes()
        assert first==count_row(20261020,1,1,[0,0,0,1,0,0,0,0,0])
        assert call(d,"export",20261020)["RESULT"]==OK
        assert (d / "data/count.txt").read_bytes()==first
        assert call(d,"export",20261018)["RESULT"]==OK
        assert call(d,"export",20261022)["RESULT"]==OK
        assert call(d,"cancel",1,who="U1")["BOOKING"]==0
        snapshot=(d / "data/count.txt").read_bytes()
        assert day(call(d,"daily",20261020))[1]==0
        assert (d / "data/count.txt").read_bytes()==snapshot  # 只读不偷偷修复快照。
        assert call(d,"export",20261020)["RESULT"]==OK
        assert (d / "data/count.txt").read_bytes()==count_row(20261018)+count_row(20261020)+count_row(20261022)
        assert b"U1" not in snapshot and "张三".encode() not in snapshot
    check("首次导出/重复upsert/保留其他日期/有序/取消后快照过时/重新导出",export_update)

    good=count_row(20261018,1,1)
    corrupt=[b"bad\n",count_row(20260230),count_row(20261019,361,1),count_row(20261019,1,0),
             count_row(20261019,1,2),count_row(20261019,2,1,[2]+[0]*8),
             count_row(20261019,1,1,[0]*9),count_row(20261019,1,1,[41]+[0]*8),
             good+good,count_row(20261020)+good,good+b"\n",good.replace(b"|1|1|1|",b"|1|1|2|"),
             b"\xef\xbb\xbf"+good,good.replace(b"|1|",b"|-1|",1),good.replace(b"|1|",b"|2147483648|",1),
             good.replace(b"|1|",b"|+1|",1),good.rstrip(b"\n")+b"|extra\n",good.replace(b"|1|",b"|1\x00|",1),
             good.replace(b"|1|",b"|\xff|",1),b"x"*200+b"\n"]
    for i,content in enumerate(corrupt):
        def corrupt_count(i=i,content=content):
            d=case(f"count-bad-{i}",counts=content)
            assert call(d,"load-counts")["RESULT"]==STORAGE
            assert call(d,"export",20261019)["RESULT"]==STORAGE
            assert (d / "data/count.txt").read_bytes()==content
            assert day(call(d,"daily",20261019))[1]==0
        check(f"损坏快照拒绝覆盖/加载输出不变/实时不依赖快照 {i}",corrupt_count)
    for kind in range(8):
        def bad_save(kind=kind):
            d=case(f"count-save-invalid-{kind}",counts=good)
            assert call(d,"bad-save",20261019,kind)["RESULT"]==OK
        check(f"统计保存接口独立完整验证 {kind}",bad_save)
    def crlf():
        d=case("count-crlf",counts=good.replace(b"\n",b"\r\n")+count_row(20261020).rstrip(b"\n"))
        assert call(d,"load-counts")["COUNT"]==2
        assert call(d,"export",20261019)["RESULT"]==OK
        assert (d / "data/count.txt").read_bytes()==good+count_row(20261019)+count_row(20261020)
    check("统计文件CRLF/末行无换行/规范LF保存",crlf)

    for source in ["reservations.txt","admins.txt","students.txt"]:
        def corrupt_source(source=source):
            d=case("source-"+source)
            (d / "data" / source).write_bytes(b"bad\n")
            modes=["student"] if source=="students.txt" else (["daily","week","student","export"] if source=="reservations.txt" else ["daily","week","export"])
            for mode in modes:
                who="rawstudent" if mode=="student" else "rawadmin"
                assert call(d,mode,20261019,who=who)["RESULT"]==STORAGE
        check("损坏来源文件不能解释为零 "+source,corrupt_source)

    for kind in ["write","flush","close","crash"]:
        def io_failure(kind=kind):
            d=case("export-io-"+kind,reservation(1,date=20261019),counts=good)
            q=call(d,"export",20261019,env={"LAB_TEST_IO_FAILURE":kind},exit_code=86 if kind=="crash" else 0)
            if kind!="crash":
                assert q["RESULT"]==STORAGE and q["INJECTED"]==1
            assert (d / "data/count.txt").read_bytes()==good
            assert day(call(d,"daily",20261019))[1]==1
            assert (d / "data/count.txt.tmp").is_file()
            # 明确保留该失败草稿后再试；不删除、不递归操作。
            (d / "data/count.txt.tmp").rename(d / "data/count.failed-backup.txt")
            assert call(d,"export",20261019)["RESULT"]==OK
        check("导出真实I/O失败或进程中断/旧文件保留/重试 "+kind,io_failure)

    for kind in ["stale","directory"]:
        def temp_block(kind=kind):
            d=case("temp-"+kind,counts=good)
            temp=d / "data/count.txt.tmp"
            if kind=="stale": temp.write_bytes(b"keep")
            else: temp.mkdir()
            assert call(d,"export",20261019)["RESULT"]==STORAGE
            assert (d / "data/count.txt").read_bytes()==good
            if kind=="stale": assert temp.read_bytes()==b"keep"
        check("导出遗留临时路径不覆盖 "+kind,temp_block)

    def recovery_gate():
        b=reservation(1,date=20261020)+reservation(2,date=20261018)
        d=case("recovery-gate",b,good)
        call(d,"crash-maintenance",env={"LAB_TEST_STAGE":"3","LAB_TEST_IO_FAILURE":"crash"},exit_code=86)
        journal=(d / "data/maintenance.txt").read_bytes()
        for mode in ["daily","week","student","export"]:
            assert call(d,mode,20261020,who="U1" if mode=="student" else "admin")["RESULT"]==RECOVERY
        assert call(d,"count-gate")["RESULT"]==OK
        assert (d / "data/maintenance.txt").read_bytes()==journal
        assert call(d,"recover")["RESULT"]==OK
        assert day(call(d,"daily",20261020))[1]==0
        assert list(map(int,call(d,"student",who="U1")["STUDENT"][0]))==[1,1,1,0,1]
        assert call(d,"export",20261020)["RESULT"]==OK
    check("真实P3中断/四种统计门禁/日志不清理/恢复后重新统计",recovery_gate)

    if os.name=="nt":
        import ctypes
        from ctypes import wintypes
        kernel=ctypes.WinDLL("kernel32",use_last_error=True)
        kernel.CreateFileW.argtypes=[wintypes.LPCWSTR,wintypes.DWORD,wintypes.DWORD,wintypes.LPVOID,wintypes.DWORD,wintypes.DWORD,wintypes.HANDLE]
        kernel.CreateFileW.restype=wintypes.HANDLE
        kernel.CloseHandle.argtypes=[wintypes.HANDLE];kernel.CloseHandle.restype=wintypes.BOOL
        for filename,sharing,mode in [("count.txt",3,"export"),("count.txt",0,"export"),("reservations.txt",0,"daily")]:
            def lock(filename=filename,sharing=sharing,mode=mode):
                d=case(f"lock-{filename}-{sharing}",counts=good)
                handle=kernel.CreateFileW(str(d / "data" / filename),0x80000000,sharing,None,3,0x80,None)
                assert handle!=ctypes.c_void_p(-1).value
                try:
                    # 锁住count时run不预读该文件，操作后再验证原文。
                    r=run(d,[str(probe),mode,"admin","20261019"],allow=(filename,))
                    assert b"RESULT=4" in r.stdout
                finally: assert kernel.CloseHandle(handle)
                assert (d / "data/count.txt").read_bytes()==good
                if (d / "data/count.txt.tmp").is_file():
                    (d / "data/count.txt.tmp").rename(d / "data/count.locked-backup.txt")
                assert call(d,"export",20261019)["RESULT"]==OK
            check(f"真实Windows读取/替换失败及解除后恢复 {filename}/{sharing}",lock)

    for mode in ["daily","week","student","export","load-counts"]:
        def memory(mode=mode):
            bookings=reservation(1,date=20261019)+reservation(2,"U2",2,date=20261020)
            d=case("alloc-count-"+mode,bookings,good)
            who="U1" if mode=="student" else "admin"
            total=call(d,mode,20261019,who=who)["ALLOCATIONS"]
            assert total>0
            for nth in range(1,total+1):
                d=case(f"alloc-{mode}-{nth}",bookings,good)
                q=call(d,mode,20261019,who=who,env={"LAB_TEST_FAIL_ALLOC":str(nth)})
                assert q["RESULT"] in [MEMORY,STORAGE]
                assert (d / "data/count.txt").read_bytes()==good
        check("逐次动态分配失败/输出哨兵/释放/旧文件保留 "+mode,memory)

    def max_reservations():
        b=b"".join(reservation(i,date=20000101,status=0) for i in range(1,100000))
        b+=reservation(100000,date=20261019,slot=0)
        d=case("max-reservations",b)
        assert day(call(d,"daily",20261019))[1:5]==[1,1,1,1]
        assert int(call(d,"week",20261019)["WEEK"][0][2])==1
        assert list(map(int,call(d,"student",who="U1")["STUDENT"][0]))==[1,1,1,0,99999]
    check("真实十万预约/大历史动态内存/整数边界安全",max_reservations)
    def full_capacity():
        b=b"";identifier=1
        for offset in range(7):
            for slot in range(9):
                for no in range(1,41):
                    b+=reservation(identifier,f"S{no}-{slot}",no,date=20261019+offset,slot=slot);identifier+=1
        d=case("full-capacity",b,faults=range(1,41))
        row=day(call(d,"daily",20261019))
        assert row[1:4]==[360,360,360] and row[5]==100.0 and row[6:]==[40]*9
        w=call(d,"week",20261019)["WEEK"][0]
        assert list(map(int,w[2:4]))==[2520,360] and float(w[4])==360.0 and float(w[5])==100.0
    check("理论每日360/每周2520/百分比100/历史不依赖当前故障/已离开账户的预约事实",full_capacity)

    for extra in [0,1]:
        def count_capacity(extra=extra):
            start=datetime.date(1000,1,1)
            content=b"".join(count_row(int((start+datetime.timedelta(days=i)).strftime("%Y%m%d"))) for i in range(100000+extra))
            d=case(f"count-capacity-{extra}",counts=content)
            assert call(d,"load-counts")["RESULT"]==(OK if extra==0 else CAPACITY)
            assert call(d,"export",20261019)["RESULT"]==CAPACITY
            assert (d / "data/count.txt").read_bytes()==content
            if extra==0:
                assert call(d,"export",10000101)["RESULT"]==OK
                assert (d / "data/count.txt").read_bytes()==content
        check(f"快照集合容量100000+{extra}/安全拒绝越界",count_capacity)

    def ui_admin():
        d=case("ui-admin",reservation(1,date=20261019,slot=0))
        actions=[5,1,20261019,2,20261019,1,3,20261022,0,6,3,4]
        r=run(d,[str(ui)],lines(1,2,"Admin",PASSWORD,*actions),allow=("count.txt",))
        for text in ["每日预约统计","360 座位小时","预约利用率：0.28%","导出成功","每周预约统计","整周去重学生","不代表实际"]:
            assert text.encode() in r.stdout
        assert (d / "data/count.txt").read_bytes()==count_row(20261019,1,1)
    check("管理员UI日报/确认导出/周统计/两位小数/说明",ui_admin)
    def ui_student():
        d=case("ui-student",reservation(1,date=20261019,slot=0))
        r=run(d,[str(ui)],lines(2,2,"U1",PASSWORD,6,7,3,4))
        for text in ["我的实验室预约统计","累计完成预约：1 小时","姓名：张三","尚未完成","不代表实际"]:
            assert text.encode() in r.stdout
    check("学生UI本人姓名/完成小时/待完成/统计说明",ui_student)
    for i,actions in enumerate([[5],[5,1],[5,2],[5,2,20261019],[5,1,20260230],[5,1,"x"*500],[5,"abc"],[5,2,20261019,0,0,6,3,4]]):
        def ui_input(i=i,actions=actions):
            d=case(f"ui-input-{i}")
            r=run(d,[str(ui)],lines(1,2,"Admin",PASSWORD,*actions))
            assert "导出成功".encode() not in r.stdout
            assert (d / "data/count.txt").read_bytes()==b""
        check(f"统计UI非法日期/超长/非数字/EOF/放弃 {i}",ui_input)
    def ui_failure():
        d=case("ui-failure",bookings=b"bad\n")
        r=run(d,[str(ui)],lines(1,2,"Admin",PASSWORD,5,1,20261019,0,6,3,4))
        assert "统计失败".encode() in r.stdout and "有效预约：0".encode() not in r.stdout
    check("UI损坏源只报错不输出虚假零结果",ui_failure)
    def single_clock():
        b=b"".join(reservation(i+1,date=20261019+i,slot=0) for i in range(7))
        d=case("single-clock",b)
        q=call(d,"week",20261022,env={"LAB_TEST_NOW":"20261019,10,15,0","LAB_TEST_LATER":"20261025,17,0,0"})
        assert [day(q,i)[4] for i in range(7)]==[1,0,0,0,0,0,0]
    check("整周只取一次时钟/所有日期完成小时使用相同时刻",single_clock)

    for filename,mode in [("count.txt","export"),("reservations.txt","daily"),("reservations.txt","student")]:
        def missing_file(filename=filename,mode=mode):
            d=case("missing-file-"+filename+"-"+mode,counts=good)
            path=d / "data" / filename
            backup=d / "data" / (filename+".missing-backup")
            path.rename(backup)
            assert call(d,mode,20261019,who="U1" if mode=="student" else "admin")["RESULT"]==STORAGE
            assert not path.exists()
            backup.rename(path)
            assert call(d,mode,20261019,who="U1" if mode=="student" else "admin")["RESULT"]==OK
        check("运行中来源缺失拒绝/不补建/恢复来源后重试 "+filename+"/"+mode,missing_file)

    def untouched():
        assert original=={p.name:p.read_bytes() for p in (root / "data").iterdir() if p.is_file()}
    check("全部只读保护原文件和修改时间/真实开发数据不变",untouched)

    passed=sum(r["passed"] for r in results)
    (run_root / "results.json").write_text(json.dumps({"phase":"P4","passed":passed,"total":len(results),"results":results},ensure_ascii=False,indent=2),encoding="utf-8")
    text=f"# P4 实际执行结果\n\n通过：{passed}/{len(results)}\n\n"+"\n".join(f"- {'PASS' if r['passed'] else 'FAIL'} {r['name']} {r['detail']}".rstrip() for r in results)+"\n"
    assert PASSWORD not in text
    (run_root / "TEST_RESULTS.md").write_text(text,encoding="utf-8")
    print(f"\n{passed}/{len(results)} passed; report: {run_root}")
    return int(passed!=len(results))


if __name__=="__main__":
    raise SystemExit(main())
