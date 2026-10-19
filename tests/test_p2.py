"""P2 固定时钟、真实文件、分配/I/O 故障及 UI 集成测试；不清理目录，不记录密码。"""
import argparse
import ctypes
import datetime
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import traceback
import uuid

OK, INVALID, UNAUTHORIZED, MISSING_STUDENT, FAULT, OCCUPIED, CONFLICT, NOT_FOUND, NOT_OWNER, CANCELLED, EXPIRED, WINDOW, STORAGE, CAPACITY, MEMORY, ID_LIMIT, CLOCK = range(17)
PASSWORD = "P2Synthetic!"  # 合成凭据只用于隔离账户文件，不写任何日志。


def lines(*values):
    """参数：物理行（字节或文本）。返回：测试 stdin，不记录。"""
    return b"\n".join(v if isinstance(v, bytes) else str(v).encode() for v in values) + b"\n"


def record(identifier=1, student="U1", computer=1, date=20261020, slot=3, status=1):
    """参数为预约事实；返回持久化测试行，不能包含密码。"""
    return lines(f"{identifier}|{student}|{computer}|{date}|{slot}|{status}")


def main():
    """构建测试驱动并执行隔离用例；返回失败状态。"""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", default="smart-lab.exe")
    parser.add_argument("--cc", default="gcc")
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    executable = Path(args.executable).resolve()
    compiler = shutil.which(args.cc)
    if not compiler or not Path(args.executable).is_file():
        parser.error("需要 GCC 与已编译的程序。")
    run_root = root / "tests/runs" / ("p2-" + datetime.datetime.now().strftime("%Y%m%d-%H%M%S-") + uuid.uuid4().hex[:8])
    run_root.mkdir(parents=True)
    original_data = {p.name: p.read_bytes() for p in (root / "data").iterdir() if p.is_file()}
    probe, ui = run_root / "booking-probe.exe", run_root / "fixed-ui.exe"
    flags = [compiler, "-std=c11", "-Wall", "-Wextra", "-Wpedantic", "-Werror", "-I", str(root / "include")]
    sources = [str(root / "src" / f"{name}.c") for name in ["auth", "storage", "utils", "lab", "booking"]]
    builds = [flags + ["-include", str(root / "tests/io_faults.h"), str(root / "tests/booking_probe.c"), *sources,
                      "-Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc", "-o", str(probe)],
              flags + [str(root / "tests/booking_ui_main.c"), str(root / "src/ui.c"), *sources, "-o", str(ui)]]
    for i, command in enumerate(builds):
        result = subprocess.run(command, capture_output=True, timeout=60)
        (run_root / f"build-{i}.log").write_bytes(result.stdout + result.stderr)
        if result.returncode:
            print("Driver build failed; inspect build log", i)
            return 1
    results = []

    def case(name, content=b"", faults=()):
        """创建完整隔离数据集，参数为用例名、预约原文和故障编号；返回目录。"""
        directory = run_root / name
        data = directory / "data"
        data.mkdir(parents=True)
        (data / "computers.txt").write_bytes(b"".join(lines(f"{n}|{int(n in faults)}") for n in range(1, 41)))
        (data / "students.txt").write_bytes(b"".join(lines(f"{s}|学生|{PASSWORD}|12") for s in ["U1", "U2", "U3"]))
        (data / "admins.txt").write_bytes(lines(f"Admin|管理员|{PASSWORD}"))
        (data / "reservations.txt").write_bytes(content)
        for filename in ["error.txt", "count.txt"]:
            (data / filename).write_bytes(b"")
        return directory

    def run(directory, command, data=b"", env=None):
        """运行隔离进程，只记录脱敏输出，并检查账户/统计文件未变。返回结果。"""
        environment = os.environ.copy()
        for key in list(environment):
            if key.startswith("LAB_TEST_"):
                environment.pop(key)
        environment.update(env or {})
        protected = [directory / "data" / name for name in ["students.txt", "admins.txt", "error.txt", "count.txt"]]
        before = {p: p.read_bytes() for p in protected if p.is_file()}
        result = subprocess.run(command, input=data, cwd=directory, env=environment, capture_output=True, timeout=30)
        assert PASSWORD.encode() not in result.stdout + result.stderr
        index = len(list(directory.glob("stdout-*.txt"))) + 1
        (directory / f"stdout-{index}.txt").write_bytes(result.stdout)
        (directory / f"stderr-{index}.txt").write_bytes(result.stderr)
        assert all(p.read_bytes() == value for p, value in before.items())
        assert result.returncode == 0
        return result

    def call(directory, mode, *values, who="U1", env=None):
        """调用 API 驱动并解析不含密码的键值。返回字典（包含原始 stdout/stderr）。"""
        result = run(directory, [str(probe), mode, who, *map(str, values)], env=env)
        parsed = {"stdout": result.stdout, "stderr": result.stderr}
        for key, value in re.findall(rb"^([A-Z]+)=(.*)$", result.stdout, re.M):
            value = value.rstrip(b"\r")  # Windows stdout 为 CRLF，字段解析不能保留 CR。
            if key == b"ROW":
                parsed.setdefault("ROWS", []).append(value.decode().split(","))
            else:
                parsed[key.decode()] = int(value) if re.fullmatch(rb"-?\d+", value) else value.decode()
        return parsed

    def ui_run(directory, actions, env=None):
        """固定时钟 UI；自动登录 U1，actions 为登录后的操作，不记录输入。"""
        return run(directory, [str(ui)], lines(2, 2, "U1", PASSWORD, *actions), env)

    def check(name, function):
        """执行并记录单个用例，失败只输出类型/行号，防止断言泄露输入。"""
        try:
            function()
            results.append({"name": name, "passed": True, "detail": ""})
            print("PASS", name)
        except Exception as error:
            line = traceback.extract_tb(error.__traceback__)[-1].lineno
            detail = f"{type(error).__name__} at line {line}"
            results.append({"name": name, "passed": False, "detail": detail})
            print("FAIL", name, detail)

    def normal():
        """同电脑不同时段/日期与跨进程持久化，编号唯一，两种冲突均拒绝。"""
        d = case("normal")
        assert call(d, "create", 20261019, 3, 1)["ID"] == 1
        assert call(d, "create", 20261019, 3, 1, who="U2")["RESULT"] == OCCUPIED
        assert call(d, "create", 20261019, 3, 2)["RESULT"] == CONFLICT
        assert call(d, "create", 20261019, 4, 1)["ID"] == 2
        assert call(d, "create", 20261020, 3, 1)["ID"] == 3
        assert call(d, "create", 20261020, 3, 2, who="U2")["ID"] == 4
        rows = call(d, "query", 0)["ROWS"]
        assert [int(r[0]) for r in rows] == [1, 2, 3] and all(r[1] == "U1" for r in rows)
        view = call(d, "seats", 20261020, 3)
        assert view["SEATS"] == "##" + "*" * 38
        assert call(d, "seats", 20261020, 4)["SEATS"] == "*" * 40
        assert call(d, "load")["COUNT"] == 4
    check("正常创建/不同日期时段/唯一编号/两类冲突/重启", normal)

    def cancellation():
        """取消不删除，重复/他人拒绝，座位释放且新编号不复用。"""
        d = case("cancellation", record())
        assert call(d, "cancel", 1, who="U2")["RESULT"] == NOT_OWNER
        assert call(d, "check-cancel", 1)["RESULT"] == OK
        assert call(d, "cancel", 1)["RESULT"] == OK
        assert call(d, "cancel", 1)["RESULT"] == CANCELLED
        assert call(d, "cancel", 999)["RESULT"] == NOT_FOUND
        assert call(d, "seats", 20261020, 3)["SEATS"] == "*" * 40
        assert call(d, "query", 0)["COUNT"] == 0
        assert call(d, "query", 1)["ROWS"][0][-1] == "0"
        assert call(d, "create", 20261020, 3, 1)["ID"] == 2
        assert len((d / "data/reservations.txt").read_bytes().splitlines()) == 2
    check("本人取消/他人和重复拒绝/取消保留/座位释放/重新预约", cancellation)

    for date, slot in [(20261018, 8), (20261019, 2), (20261019, 0)]:
        def expired_cancel(date=date, slot=slot):
            """已过去或已经开始的有效记录保留，不能取消。"""
            content = record(date=date, slot=slot)
            d = case(f"cancel-expired-{date}-{slot}", content)
            assert call(d, "cancel", 1)["RESULT"] == EXPIRED
            assert (d / "data/reservations.txt").read_bytes() == content
            assert call(d, "query", 0)["COUNT"] == 1
            assert call(d, "query", 2)["COUNT"] == 0
        check(f"取消开始/过去拒绝 {date}-{slot}", expired_cancel)

    def future_cancel():
        """旧记录超过当前创建窗口但尚未开始，仍可取消，不误用创建窗口。"""
        d = case("cancel-beyond-window", record(date=20261130))
        assert call(d, "cancel", 1)["RESULT"] == OK
    check("尚未开始的远期旧记录可取消", future_cancel)

    time_cases = [
        (20261018, 8, None, EXPIRED), (20261019, 2, None, EXPIRED), (20261019, 3, None, OK),
        (20261020, 0, None, OK), (20261025, 8, None, OK), (20261026, 0, None, WINDOW),
        (20261301, 0, None, INVALID), (20260431, 0, None, INVALID), (20260229, 0, None, INVALID),
        (20261020, -1, None, INVALID), (20261020, 9, None, INVALID),
        (20261019, 3, "20261019,11,0,0", EXPIRED), (20261019, 3, "20261019,10,59,59", OK),
        (20240229, 0, "20240228,10,15,0", OK), (19000229, 0, "19000228,10,15,0", INVALID),
        (20000229, 0, "20000228,10,15,0", OK), (21000229, 0, "21000228,10,15,0", INVALID),
        (20270106, 0, "20261231,16,30,0", OK), (20270107, 0, "20261231,16,30,0", WINDOW),
        (20240305, 0, "20240228,16,30,0", OK), (20240306, 0, "20240228,16,30,0", WINDOW),
        (99991231, 8, "99991230,10,15,0", OK),
        (20261019, 0, "20261019,7,59,59", OK),
        (20261019, 0, "20261019,8,0,0", EXPIRED),
        (20261019, 8, "20261019,16,0,0", EXPIRED),
    ]
    for i, (date, slot, now, expected) in enumerate(time_cases):
        def time_case(i=i, date=date, slot=slot, now=now, expected=expected):
            """固定时钟验证公历与窗口，失败不创建记录。"""
            d = case(f"time-{i}")
            assert call(d, "create", date, slot, 1, env={} if now is None else {"LAB_TEST_NOW": now})["RESULT"] == expected
            if expected != OK:
                assert (d / "data/reservations.txt").read_bytes() == b""
        check(f"时间规则 {date}/slot={slot}/now={now or '20261019,10,15,0'}", time_case)

    for i, (date, delta, expected) in enumerate([(20240228,1,20240229), (19000228,1,19000301),
            (20000228,1,20000229), (20261231,1,20270101), (20270101,-1,20261231),
            (10000101,-1,0), (99991231,1,0), (20260229,1,0)]):
        def calendar(i=i, date=date, delta=delta, expected=expected):
            """日期推进跨月年和边界，时段格式缓存容量也检查。"""
            assert call(case(f"calendar-{i}"), "calendar", date, delta, i % 9, who="none")["NEXT"] == expected
        check(f"公历推进/时段格式 {i}", calendar)

    for op, arguments in [("create", (20261020, 3, 1)), ("cancel", (1,)), ("query", (1,))]:
        for who, expected in [("none", UNAUTHORIZED), ("admin", UNAUTHORIZED), ("ghost", MISSING_STUDENT)]:
            def permission(op=op, arguments=arguments, who=who, expected=expected):
                """直接绕过 UI 调用业务边界仍必须校验角色与真实学生。"""
                d = case(f"permission-{op}-{who}", record())
                before = (d / "data/reservations.txt").read_bytes()
                assert call(d, op, *arguments, who=who)["RESULT"] == expected
                assert (d / "data/reservations.txt").read_bytes() == before
            check(f"业务权限 {op}-{who}", permission)

    def accounts_corrupt():
        """会话仍在但学生文件损坏，预约/取消/查询不得冒充未找到。"""
        d = case("accounts-corrupt", record())
        (d / "data/students.txt").write_bytes(b"broken\n")
        for op, values in [("create", (20261020,3,2)), ("cancel",(1,)), ("query",(1,))]:
            assert call(d, op, *values, who="forged")["RESULT"] == STORAGE
    check("学生文件损坏与账户不存在分离", accounts_corrupt)

    def clock_errors():
        """时钟失败与非法本地时间必须安全拒绝，查询历史不依赖墙钟。"""
        d=case("clock-errors",record())
        for env in [{"LAB_TEST_CLOCK_ERROR":"1"},{"LAB_TEST_NOW":"20261019,24,0,0"}]:
            for mode,args in [("create",(20261020,4,2)),("cancel",(1,)),("query",(2,))]:
                assert call(d,mode,*args,env=env)["RESULT"]==CLOCK
            assert call(d,"query",1,env=env)["RESULT"]==OK
        assert (d/"data/reservations.txt").read_bytes()==record()
    check("异常时间源安全失败/历史查询不依赖时钟",clock_errors)

    def parameter_errors():
        """业务参数绕过 UI 时仍被验证。"""
        d=case("parameter-errors")
        for no in [0,41,-1]: assert call(d,"create",20261020,3,no)["RESULT"]==INVALID
        assert call(d,"cancel",0)["RESULT"]==INVALID
        assert call(d,"query",3)["RESULT"]==INVALID
        assert call(d,"seats",20260229,3)["RESULT"]==INVALID
        assert call(d,"seats",20261020,9)["RESULT"]==INVALID
    check("业务输入参数边界",parameter_errors)

    def changed_after_confirmation():
        """确认前预检不缓存授权/占位快照，正式操作重新读取当前记录。"""
        d=case("confirmation-recheck")
        path=d/"data/reservations.txt"
        assert call(d,"check-create",20261020,3,1)["RESULT"]==OK
        assert path.read_bytes()==b""
        path.write_bytes(record(student="U2"))
        assert call(d,"create",20261020,3,1)["RESULT"]==OCCUPIED
        path.write_bytes(record())
        assert call(d,"check-cancel",1)["RESULT"]==OK
        path.write_bytes(record(student="U2"))
        assert call(d,"cancel",1)["RESULT"]==NOT_OWNER
        assert path.read_bytes()==record(student="U2")
    check("确认后重新加载冲突与归属",changed_after_confirmation)

    def missing_files():
        """业务运行中缺失预约文件不是空表，缺失/空电脑数据也不静默初始化。"""
        d=case("missing-files",record()); path=d/"data/reservations.txt"
        path.rename(d/"data/reservations.saved")
        for mode,args in [("create",(20261020,4,2)),("query",(1,)),("cancel",(1,)),("seats",(20261020,3))]:
            assert call(d,mode,*args)["RESULT"]==STORAGE
        assert not path.exists()
        (d/"data/reservations.saved").rename(path)
        (d/"data/computers.txt").write_bytes(b"")
        assert call(d,"create",20261020,4,2)["RESULT"]==STORAGE
        assert (d/"data/computers.txt").read_bytes()==b""
        assert path.read_bytes()==record()
    check("业务读文件缺失/空电脑表不初始化",missing_files)

    def faults():
        """真实故障优先于文件中原有有效预约，不硬编码显示状态。"""
        d = case("faults", record(computer=10), faults=[10])
        assert call(d, "seats", 20261020, 3)["SEATS"][9] == "!"
        assert call(d, "create", 20261020, 4, 10)["RESULT"] == FAULT
        assert call(d, "seats", 20261021, 3)["COUNTS"] == "39,0,1"
    check("故障拒绝与座位符号优先级", faults)

    def sorted_query():
        """乱序个人历史/有效过滤，排序和只读字节/时间戳保持。"""
        content = record(9,date=20261021) + record(4,date=20261020,status=0) + record(7,date=20261020,slot=2,status=0)
        content += record(2,date=20261018) + record(20,student="U2",computer=4)
        d = case("sorted-query", content)
        path = d / "data/reservations.txt"; timestamp = path.stat().st_mtime_ns
        assert [int(r[0]) for r in call(d,"query",0)["ROWS"]] == [2,9]
        rows = call(d,"query",1)["ROWS"]
        assert [int(r[0]) for r in rows] == [2,7,4,9] and all(r[1] == "U1" for r in rows)
        assert [int(r[0]) for r in call(d,"query",2)["ROWS"]] == [9]
        assert path.read_bytes() == content and path.stat().st_mtime_ns == timestamp
    check("个人过滤/有效与全部历史/排序/查询不写入", sorted_query)

    valid = record()
    corruptions = {
        "garbage": b"garbage\n", "blank": b"\n", "empty-id": valid.replace(b"1|U1",b"|U1"),
        "duplicate-id": valid + record(1,student="U2",computer=2),
        "duplicate-cancelled-id": valid + record(1,status=0),
        "seat-conflict": valid + record(2,student="U2"),
        "student-conflict": valid + record(2,computer=2),
        "no-zero": valid.replace(b"|1|2026",b"|0|2026"),
        "no-large": valid.replace(b"|1|2026",b"|41|2026"),
        "bad-date": valid.replace(b"20261020",b"20260229"),
        "long-date": valid.replace(b"20261020",b"020261020"),
        "bad-slot": valid.replace(b"|3|1",b"|9|1"), "negative-slot": valid.replace(b"|3|1",b"|-1|1"),
        "bad-status": valid.replace(b"|3|1",b"|3|2"), "suffix": valid.rstrip() + b"X\n",
        "extra-field": valid.rstrip() + b"|x\n", "missing-field": b"1|U1|1|20261020|3\n",
        "overflow-id": b"999999999999999999999|U1|1|20261020|3|1\n",
        "zero-id": b"0|U1|1|20261020|3|1\n", "negative-id": b"-1|U1|1|20261020|3|1\n",
        "invalid-student": valid.replace(b"U1",b"bad id"), "long-student": valid.replace(b"U1",b"A"*32),
        "nul": valid.replace(b"U1",b"U1\x00hidden"), "bom": b"\xef\xbb\xbf"+valid,
        "huge-line": b"9"*10000+b"\n", "numeric-space": valid.replace(b"|3|",b"| 3|"),
    }
    for name, content in corruptions.items():
        def corruption(name=name, content=content):
            """严格拒绝坏文件，读失败不发布半成品，所有业务均不覆盖源。"""
            d = case("corrupt-"+name,content); path = d/"data/reservations.txt"; stamp=path.stat().st_mtime_ns
            assert call(d,"load")["STORAGE"] == 1
            for op, values in [("create",(20261020,4,2)),("query",(1,)),("cancel",(1,)),("seats",(20261020,3))]:
                assert call(d,op,*values)["RESULT"] == STORAGE
            assert path.read_bytes() == content and path.stat().st_mtime_ns == stamp
            assert not (d/"data/reservations.txt.tmp").exists()
        check("损坏预约保护 "+name,corruption)

    def legacy():
        """乱序/CRLF/末行无换行，取消记录不冲突，最大编号包括取消记录。"""
        content = record(10,status=0).replace(b"\n",b"\r\n") + record(2,date=20261021).rstrip(b"\n")
        d = case("legacy",content)
        assert call(d,"load")["COUNT"] == 2
        assert call(d,"create",20261020,3,1)["ID"] == 11
        assert call(d,"load")["COUNT"] == 3
    check("乱序/CRLF/末行无换行/取消编号不复用",legacy)

    def identifiers():
        """编号整数边界，无溢出/回绕/复用。"""
        d=case("id-boundary",record(2147483646,status=0))
        assert call(d,"create",20261020,3,1)["ID"] == 2147483647
        before=(d/"data/reservations.txt").read_bytes()
        assert call(d,"create",20261021,3,1)["RESULT"] == ID_LIMIT
        assert (d/"data/reservations.txt").read_bytes()==before
    check("编号INT_MAX边界安全拒绝",identifiers)

    for kind in range(1,11):
        def invalid_save(kind=kind):
            """绕过 UI 的无效快照也必须保存前全量拒绝。"""
            content=record(1,status=0)+record(2,status=0)
            d=case(f"save-invalid-{kind}",content)
            assert call(d,"validate",kind)["STORAGE"] == (3 if kind==10 else 1)
            assert (d/"data/reservations.txt").read_bytes()==content
            assert not (d/"data/reservations.txt.tmp").exists()
        check(f"保存前完整校验 {kind}",invalid_save)

    for extra in [0,1]:
        def capacity(extra=extra):
            """十万条真实记录边界，超容量加载安全失败，不是小常量替身测试。"""
            content=b"".join(record(i,date=20000101,status=0) for i in range(1,100001+extra))
            d=case(f"capacity-{extra}",content)
            loaded=call(d,"load")
            assert loaded["STORAGE"] == (0 if extra==0 else 3)
            if extra==0: assert loaded["COUNT"]==100000
            assert call(d,"create",20261020,3,1)["RESULT"]==CAPACITY
            assert (d/"data/reservations.txt").read_bytes()==content
        check(f"真实MAX_RESERVATIONS边界 +{extra}",capacity)

    memory_fixture=b"".join(record(i,date=20000101,status=0) for i in range(1,64))+record(100,computer=2,slot=4)
    for mode, values in [("create",(20261020,3,1)),("cancel",(100,)),("query",(1,)),("seats",(20261020,4))]:
        base=case("allocation-base-"+mode,memory_fixture)
        allocations=call(base,mode,*values)["ALLOCATIONS"]
        assert allocations>0
        for nth in range(1,allocations+1):
            def allocation_failure(mode=mode,values=values,nth=nth):
                """逐个实际分配点失败，不能误报成功或改变预约文件。"""
                d=case(f"alloc-{mode}-{nth}",memory_fixture)
                result=call(d,mode,*values,env={"LAB_TEST_FAIL_ALLOC":str(nth)})
                assert result["RESULT"] in [MEMORY,STORAGE]
                assert (d/"data/reservations.txt").read_bytes()==memory_fixture
                assert not (d/"data/reservations.txt.tmp").exists()
            check(f"分配故障 {mode}-{nth}",allocation_failure)
        def recovery(mode=mode,values=values):
            """同一进程解除内存故障后可直接重试，不留幽灵预约/坏指针。"""
            d=case("memory-recovery-"+mode,memory_fixture)
            result=call(d,mode,*values,env={"LAB_TEST_FAIL_ALLOC":"1","LAB_TEST_RECOVER":"1"})
            assert result["RESULT"]==MEMORY and result["RECOVERY"]==OK
        check("内存故障同进程恢复 "+mode,recovery)
    for nth in [1,2,3]:
        def partial_load(nth=nth):
            """读取65条跨扩容点，在初分配/再分配/索引分配失败时保留原输出。"""
            content=b"".join(record(i,date=20000101,status=0) for i in range(1,66))
            assert call(case(f"partial-load-{nth}",content),"load",env={"LAB_TEST_FAIL_ALLOC":str(nth)})["STORAGE"]==2
        check(f"部分读取/扩容失败输出保持 {nth}",partial_load)
    check("realloc失败保留原指针并可恢复",lambda: call(case("reserve"),"reserve"))

    for mode, values in [("create",(20261020,4,2)),("cancel",(1,))]:
        for kind in ["stale","directory","write","flush","close"]:
            def write_failure(mode=mode,values=values,kind=kind):
                """临时创建、逐条写入、刷新、关闭失败均不得提交新状态。"""
                d=case(f"io-{mode}-{kind}",valid); temp=d/"data/reservations.txt.tmp"
                env={}
                if kind=="stale": temp.write_bytes(b"keep")
                elif kind=="directory": temp.mkdir()
                else: env={"LAB_TEST_IO_FAILURE":kind}
                assert call(d,mode,*values,env=env)["RESULT"]==STORAGE
                assert (d/"data/reservations.txt").read_bytes()==valid
                assert call(d,"seats",20261020,3)["SEATS"][0]=="#"
                if kind=="stale": assert temp.read_bytes()==b"keep"
            check(f"写失败回滚 {mode}-{kind}",write_failure)

    if os.name=="nt":
        from ctypes import wintypes
        kernel=ctypes.WinDLL("kernel32",use_last_error=True)
        kernel.CreateFileW.argtypes=[wintypes.LPCWSTR,wintypes.DWORD,wintypes.DWORD,wintypes.LPVOID,wintypes.DWORD,wintypes.DWORD,wintypes.HANDLE]
        kernel.CreateFileW.restype=wintypes.HANDLE
        kernel.CloseHandle.argtypes=[wintypes.HANDLE]; kernel.CloseHandle.restype=wintypes.BOOL
        for mode, values, sharing in [("create",(20261020,4,2),3),("cancel",(1,),3),("query",(1,),0)]:
            def locked(mode=mode,values=values,sharing=sharing):
                """真实 Windows 文件锁：拒绝替换或读取，源文件完全保持。"""
                d=case("windows-lock-"+mode,valid); path=d/"data/reservations.txt"
                handle=kernel.CreateFileW(str(path),0x80000000,sharing,None,3,0x80,None)
                assert handle!=ctypes.c_void_p(-1).value
                try: assert call(d,mode,*values)["RESULT"]==STORAGE
                finally: assert kernel.CloseHandle(handle)
                assert path.read_bytes()==valid
            check("真实Windows锁失败 "+mode,locked)

    for op,values in [("create",(20261019,3,2)),("cancel",(1,))]:
        def crossing(op=op,values=values):
            """最终写入前时间已到开始点，拒绝状态变更。"""
            content=record(date=20261019)
            d=case("clock-crossing-"+op,content)
            assert call(d,op,*values,who="U2" if op=="create" else "U1",
                        env={"LAB_TEST_NOW":"20261019,10,59,59","LAB_TEST_LATER":"20261019,11,0,0"})["RESULT"] == EXPIRED
            assert (d/"data/reservations.txt").read_bytes()==content
        check("写入时间边界重查 "+op,crossing)

    def ui_lifecycle():
        """真实 UI 显示、确认创建、查询、取消确认与座位释放。"""
        d=case("ui-lifecycle")
        result=ui_run(d,[2,20261020,3,12,1,1,20261020,3,3,0,4,1,1,1,20261020,3,3,1,7,3,4])
        assert "预约成功".encode() in result.stdout and "取消成功".encode() in result.stdout
        assert b"12[#]" in result.stdout and b"12[*]" in result.stdout
        assert "已取消".encode() in result.stdout and "登录状态已清除".encode() in result.stdout
        assert (d/"data/reservations.txt").read_bytes()==record(computer=12,status=0)
    check("固定时间UI完整预约/查询/取消/释放",ui_lifecycle)

    def abandon():
        """两种确认放弃都不修改预约。"""
        d=case("ui-abandon",valid)
        result=ui_run(d,[2,20261020,4,12,0,4,1,0,7,3,4])
        assert "已放弃预约".encode() in result.stdout and "已放弃取消".encode() in result.stdout
        assert (d/"data/reservations.txt").read_bytes()==valid
    check("UI预约与取消放弃确认不写入",abandon)

    eof_actions=[[1],[1,20261020],[2],[2,20261020],[2,20261020,3],[2,20261020,3,12],
                 [3],[4],[4,1]]
    for i,actions in enumerate(eof_actions):
        def ui_eof(i=i,actions=actions):
            """输入结束在日期/时段/电脑/确认/查询/取消路径传播并清会话。"""
            d=case(f"ui-eof-{i}",valid)
            result=ui_run(d,actions)
            assert "输入结束".encode() in result.stdout
            assert (d/"data/reservations.txt").read_bytes()==valid
            assert "预约成功".encode() not in result.stdout and "取消成功".encode() not in result.stdout
        check(f"UI业务EOF清理 {i}",ui_eof)

    for i,action in enumerate([[2,"bad"],[2,20260229],[2,20261020,9],[2,20261020,3,41],[2,b"9"*10000]]):
        def invalid_ui(i=i,action=action):
            """非法输入整行消费，不创建记录，后续能退出。"""
            d=case(f"ui-invalid-{i}")
            result=ui_run(d,[*action,7,3,4])
            assert "输入无效".encode() in result.stdout and (d/"data/reservations.txt").read_bytes()==b""
            assert "登录状态已清除".encode() in result.stdout
        check(f"UI预约非法输入 {i}",invalid_ui)

    def ui_corrupt():
        """UI 损坏查询不显示成暂无预约，也不提供虚假成功。"""
        d=case("ui-corrupt",b"bad\n")
        result=ui_run(d,[3,4,1,20261020,3,7,3,4])
        assert "暂无".encode() not in result.stdout
        assert "数据文件损坏".encode() in result.stdout and result.stderr
    check("UI损坏文件不能显示暂无记录",ui_corrupt)

    def production_smoke():
        """正式可执行文件使用真实入口，执行不依赖当前时刻的历史查询/座位流程。"""
        content=record(date=20000101)
        d=case("production-smoke",content)
        result=run(d,[str(executable)],lines(2,2,"U1",PASSWORD,3,0,1,20000101,3,7,3,4))
        assert "我的有效预约".encode() in result.stdout and b"01[#]" in result.stdout
        assert "登录状态已清除".encode() in result.stdout
        assert (d/"data/reservations.txt").read_bytes()==content
    check("正式程序P2只读查询与日期座位流程",production_smoke)

    def real_data():
        """全部测试前后真实开发数据逐字节保持。"""
        assert original_data=={p.name:p.read_bytes() for p in (root/"data").iterdir() if p.is_file()}
    check("真实开发数据及学生累计时间不变",real_data)
    failures=sum(not r["passed"] for r in results)
    summary={"platform":os.name,"compiler":compiler,"total":len(results),"passed":len(results)-failures,"failed":failures,"cases":results}
    (run_root/"results.json").write_text(json.dumps(summary,ensure_ascii=False,indent=2),encoding="utf-8")
    report=["# P2 固定时间测试报告","",f"总计 {len(results)}；通过 {len(results)-failures}；失败 {failures}。","",
            "测试时间来自显式注入；无密码输入或账户原文。所有运行文件保留在本机隔离目录。","","| 用例 | 结果 |","|---|---|"]
    report += [f"| {r['name']} | {'通过' if r['passed'] else '失败：'+r['detail']} |" for r in results]
    (run_root/"TEST_RESULTS.md").write_text("\n".join(report)+"\n",encoding="utf-8")
    print(f"\n{len(results)-failures}/{len(results)} passed; report: {run_root}")
    return int(failures!=0)


if __name__=="__main__":
    raise SystemExit(main())
