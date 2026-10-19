"""P3 隔离测试：真实 API/UI、固定时钟、逐次分配/写入失败、进程中断恢复；保留全部现场。"""
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

OK, INVALID, UNAUTHORIZED, MISSING, ALREADY_FAULT, ALREADY_NORMAL, DUPLICATE, NOT_FOUND, PROCESSED, STORAGE, CAPACITY, MEMORY, ID_LIMIT, CLOCK, RECOVERY = range(15)
PASSWORD = "P3Synthetic!"  # 合成凭据仅用于隔离账户，不写入日志/报告。


def lines(*values):
    """物理行转测试输入；不记录输入。"""
    return b"\n".join(v if isinstance(v, bytes) else str(v).encode() for v in values) + b"\n"


def report(i=1, student="U1", no=1, date=20261019, detail="鼠标失灵", status=0):
    """返回单条报告持久化事实。"""
    return lines(f"{i}|{student}|{no}|{date}|{detail}|{status}")


def reservation(i, student="U1", no=1, date=20261020, slot=3, status=1):
    """返回单条预约事实。"""
    return lines(f"{i}|{student}|{no}|{date}|{slot}|{status}")


LINKED = (reservation(1, date=20261018) + reservation(2, date=20261019, slot=2) +
          reservation(3, date=20261019, slot=3) + reservation(4) + reservation(5, "U2", 2))


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
    run_root = root / "tests/runs" / ("p3-" + datetime.datetime.now().strftime("%Y%m%d-%H%M%S-") + uuid.uuid4().hex[:8])
    run_root.mkdir(parents=True)
    original = {p.name: p.read_bytes() for p in (root / "data").iterdir() if p.is_file()}
    probe, ui = run_root / "fault-probe.exe", run_root / "fixed-ui.exe"
    sources = [str(root / "src" / f"{n}.c") for n in ["auth", "storage", "utils", "lab", "booking", "fault", "admin"]]
    flags = [cc, "-std=c11", "-Wall", "-Wextra", "-Wpedantic", "-Werror", "-I", str(root / "include")]
    builds = [flags + ["-include", str(root / "tests/io_faults.h"), str(root / "tests/fault_probe.c"), *sources,
                       "-Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc", "-o", str(probe)],
              flags + [str(root / "tests/booking_ui_main.c"), str(root / "src/ui.c"), str(root / "src/stats.c"), *sources, "-o", str(ui)]]
    for i, command in enumerate(builds):
        built = subprocess.run(command, capture_output=True, timeout=90)
        (run_root / f"build-{i}.log").write_bytes(built.stdout + built.stderr)
        if built.returncode:
            print("Driver build failed", i, run_root)
            return 1
    results = []

    def case(name, reports=b"", bookings=b"", faults=()):
        """新建完整隔离数据，不清理现场。"""
        d = run_root / name
        data = d / "data"
        data.mkdir(parents=True)
        (data / "computers.txt").write_bytes(b"".join(lines(f"{n}|{int(n in faults)}") for n in range(1, 41)))
        (data / "students.txt").write_bytes(b"".join(lines(f"{s}|{name}|{PASSWORD}|12") for s, name in [("U1", "张三"), ("U2", "李四"), ("U3", "王五")]))
        (data / "admins.txt").write_bytes(lines(f"Admin|管理员|{PASSWORD}"))
        (data / "reservations.txt").write_bytes(bookings)
        (data / "error.txt").write_bytes(reports)
        (data / "count.txt").write_bytes(b"")
        return d

    def run(d, command, data=b"", env=None, exit_code=0):
        """运行真实进程，凭据检查后才保存 stdout/stderr，断言保护账户/累计时间。"""
        environment = {k: v for k, v in os.environ.items() if not k.startswith("LAB_TEST_")}
        environment.update(env or {})
        protected = {n: (d / "data" / n).read_bytes() for n in ["students.txt", "admins.txt", "count.txt"]}
        r = subprocess.run(command, input=data, cwd=d, env=environment, capture_output=True, timeout=45)
        assert PASSWORD.encode() not in r.stdout + r.stderr
        number = len(list(d.glob("stdout-*.txt"))) + 1
        (d / f"stdout-{number}.txt").write_bytes(r.stdout)
        (d / f"stderr-{number}.txt").write_bytes(r.stderr)
        assert r.returncode == exit_code
        assert all((d / "data" / n).read_bytes() == content for n, content in protected.items())
        return r

    def call(d, mode, *values, who="admin", env=None, exit_code=0):
        """调用公开 API 驱动，输出只含业务事实。"""
        values = list(values)
        if mode == "submit" and len(values) > 1:
            values[1] = values[1].encode("utf-8").hex()
        r = run(d, [str(probe), mode, who, *map(str, values)], env=env, exit_code=exit_code)
        parsed = {"stdout": r.stdout, "stderr": r.stderr}
        for key, value in re.findall(rb"^([A-Z]+)=(.*)$", r.stdout, re.M):
            key, value = key.decode(), value.rstrip(b"\r").decode()
            if key in ["ROW", "REPORT", "STUDENT"]:
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

    def statuses(d):
        """读取隔离预约状态（测试断言用途）。"""
        return {int(f[0]): int(f[5]) for f in [line.split("|") for line in (d / "data/reservations.txt").read_text().splitlines()]}

    def normal():
        """上报不改电脑/预约，跨进程编号唯一，学生只见本人。"""
        d = case("normal", bookings=LINKED)
        before = {n: (d / "data" / n).read_bytes() for n in ["computers.txt", "reservations.txt"]}
        assert call(d, "submit", 1, "显示器闪烁", who="U1")["ID"] == 1
        assert call(d, "submit", 1, "第二位学生", who="U2")["ID"] == 2
        assert call(d, "submit", 2, "键盘失灵", who="U1")["ID"] == 3
        assert call(d, "submit", 1, "重复", who="U1")["RESULT"] == DUPLICATE
        assert call(d, "reports", 0, -1, who="U1")["COUNT"] == 2
        assert call(d, "reports", 0, -1)["COUNT"] == 3
        assert call(d, "reports", 1, 0)["COUNT"] == 2
        assert all((d / "data" / n).read_bytes() == content for n, content in before.items())
        assert "1|U1|1|20261019|显示器闪烁|0" in (d / "data/error.txt").read_text(encoding="utf-8")
    check("上报/持久化/重启/编号/同机重复/本人过滤/无维护副作用", normal)

    for mode, values, who in [("submit", (1, "测试"), "none"), ("submit", (1, "测试"), "admin"),
                               ("change", (1, 1), "U1"), ("preview", (1, 1), "U1"),
                               ("process", (1, 1), "U1"), ("get", (1,), "U1"),
                               ("usage", (20261020, 3), "U1"), ("student", ("U1", 1), "U1"),
                               ("reports", (0, -1), "none")]:
        def unauthorized(mode=mode, values=values, who=who):
            d = case(f"role-{mode}-{who}", report(), LINKED)
            before = {p.name: p.read_bytes() for p in (d / "data").iterdir()}
            assert call(d, mode, *values, who=who)["RESULT"] == UNAUTHORIZED
            assert before == {p.name: p.read_bytes() for p in (d / "data").iterdir()}
        check(f"业务角色隔离 {mode}/{who}", unauthorized)

    for who, mode, values in [("ghost", "submit", (1, "测试")), ("ghostadmin", "change", (1, 1))]:
        def ghost(who=who, mode=mode, values=values):
            d = case(who)
            assert call(d, mode, *values, who=who)["RESULT"] == MISSING
        check("会话账户必须真实存在 " + who, ghost)

    for i, detail in enumerate(["", "x"*256, "汉"*86, "x|y", "x\ny", "x\ty", " 前置", "尾随 ", "\ufeffBOM"]):
        def invalid_detail(i=i, detail=detail):
            d = case(f"detail-{i}")
            assert call(d, "submit", 1, detail, who="U1")["RESULT"] == INVALID
            assert (d / "data/error.txt").read_bytes() == b""
        check(f"故障说明字段拒绝 {i}", invalid_detail)
    for no in [0, -1, 41, 2147483647]:
        def invalid_no(no=no):
            d = case(f"no-{no}")
            assert call(d, "submit", no, "测试", who="U1")["RESULT"] == INVALID
            assert call(d, "change", no, 1)["RESULT"] == INVALID
        check(f"电脑编号边界 {no}", invalid_no)

    def valid_utf8():
        d = case("utf8")
        assert call(d, "submit", 1, "汉"*85, who="U1")["RESULT"] == OK
        assert call(d, "reports", 0, -1)["REPORT"][0][-1] == "汉"*85
    check("255字节UTF-8说明完整往返", valid_utf8)

    def clock_error():
        d = case("clock")
        assert call(d, "submit", 1, "测试", who="U1", env={"LAB_TEST_CLOCK_ERROR": "1"})["RESULT"] == CLOCK
        assert call(d, "change", 1, 1, env={"LAB_TEST_NOW": "20260230,10,0,0"})["RESULT"] == CLOCK
    check("失败及非法时钟拒绝写入", clock_error)

    corruptions = [b"bad\n", report(0), report(-1), report(2147483648), report(student="Bad|Id"),
                   report(no=0), report(no=41), report(date=20260230), report(date=99999999), report(status=2),
                   report(detail=""), report(detail="x"*256), report(detail="x\ty"), report(detail="x|y"),
                   b"1|U1|1|20261019|\xff|0\n", b"1|U1|1|20261019|x\x00y|0\n",
                   report()+report(), report()+report(2), report()+b"\n", report().rstrip(b"\n")+b"|extra\n",
                   b"+1|U1|1|20261019|x|0\n", b"1|U1|1|20261019|x|-0\n"]
    for i, content in enumerate(corruptions):
        def corrupt(i=i, content=content):
            d = case(f"corrupt-{i}", content)
            assert call(d, "load")["RESULT"] == STORAGE
            assert call(d, "submit", 2, "测试", who="U1")["RESULT"] == STORAGE
            assert call(d, "process", 1, 1)["RESULT"] == STORAGE
            assert (d / "data/error.txt").read_bytes() == content
        check(f"损坏报告拒绝/输出不变/禁止覆盖 {i}", corrupt)

    def compatible_reports():
        d = case("crlf", report(status=1).replace(b"\n", b"\r\n") + report(2, status=1))
        assert call(d, "load")["COUNT"] == 2
        assert call(d, "submit", 1, "新报告", who="U1")["ID"] == 3
    check("CRLF/已处理同机历史允许/新编号不复用", compatible_reports)
    def id_limit():
        d = case("id-limit", report(2147483647, status=1))
        assert call(d, "submit", 2, "测试", who="U1")["RESULT"] == ID_LIMIT
    check("INT_MAX报告编号耗尽无溢出", id_limit)
    for extra in [0, 1]:
        def capacity(extra=extra):
            content = b"".join(report(i, status=1) for i in range(1, 10001+extra))
            d = case(f"capacity-{extra}", content)
            loaded = call(d, "load")
            assert loaded["RESULT"] == (OK if not extra else CAPACITY)
            assert call(d, "submit", 2, "测试", who="U1")["RESULT"] == CAPACITY
            assert (d / "data/error.txt").read_bytes() == content
        check(f"报告容量边界 10000+{extra}", capacity)

    def linkage():
        d = case("linkage", report(), LINKED)
        assert [int(r[0]) for r in call(d, "preview", 1, 1)["ROW"]] == [3, 4]
        assert call(d, "change", 1, 1)["RESULT"] == OK
        assert statuses(d) == {1: 1, 2: 1, 3: 0, 4: 0, 5: 1}
        assert call(d, "change", 1, 1)["RESULT"] == ALREADY_FAULT
        assert call(d, "usage", 20261020, 3)["SEATS"].startswith("!#")
        assert call(d, "booking", 1, who="U1")["BOOKING"] == 4
        assert call(d, "submit", 1, "无需再报", who="U1")["RESULT"] == ALREADY_FAULT
        assert call(d, "reports", 1, -1, who="U1")["COUNT"] == 1
        assert call(d, "change", 1, 0)["RESULT"] == OK
        assert statuses(d) == {1: 1, 2: 1, 3: 0, 4: 0, 5: 1}
        assert call(d, "change", 1, 0)["RESULT"] == ALREADY_NORMAL
        assert call(d, "booking", 1, who="U1")["BOOKING"] == 0
        assert max(statuses(d)) == 6
        assert call(d, "get", 1)["RESULT"] == OK  # 状态维护不自动关闭报告。
        assert call(d, "reports", 0, 0)["COUNT"] == 1
    check("故障联动/过去开始记录保留/故障优先/恢复/重新预约编号", linkage)

    for now, expected in [("20261019,11,0,0", [1,2,3]), ("20261019,10,59,59", [1,2]), ("20261020,11,0,0", [1,2,3,4])]:
        def boundary(now=now, expected=expected):
            d = case("cutoff-"+now.replace(",", "-"), bookings=LINKED)
            assert call(d, "change", 1, 1, env={"LAB_TEST_NOW": now})["RESULT"] == OK
            assert [i for i,s in statuses(d).items() if s and i != 5] == expected
        check("严格开始时刻边界 " + now, boundary)

    for action in [1, 2, 3]:
        def processing(action=action):
            d = case(f"process-{action}", report(), LINKED, faults=(1,) if action == 2 else ())
            before = {n: (d / "data" / n).read_bytes() for n in ["computers.txt", "reservations.txt"]}
            assert call(d, "preview-process", 1, action)["RESULT"] == OK
            assert call(d, "process", 1, action)["RESULT"] == OK
            assert call(d, "reports", 0, 0)["COUNT"] == 0
            assert call(d, "reports", 1, 1)["COUNT"] == 1
            assert call(d, "process", 1, action)["RESULT"] == PROCESSED
            if action == 1:
                assert statuses(d)[3] == 0 and statuses(d)[4] == 0
            if action == 2:
                assert (d / "data/computers.txt").read_bytes().startswith(b"1|0")
            if action == 3:
                assert all((d / "data" / n).read_bytes() == v for n,v in before.items())
        check(f"报告动作 {action}/状态分离/重复处理", processing)

    def already_fault_report():
        d = case("report-already-fault", report(), LINKED, faults=(1,))
        assert call(d, "process", 1, 1)["RESULT"] == OK
        assert statuses(d)[3] == 0 and call(d, "reports", 0, 1)["COUNT"] == 1
    check("报告确认已故障电脑仍完成预约联动", already_fault_report)

    for mode, values, filename in [("change", (1,1), "reservations.txt"), ("process", (1,1), "computers.txt"),
                                    ("usage", (20261020,3), "students.txt"), ("student", ("U1",1), "reservations.txt")]:
        def source_bad(mode=mode, values=values, filename=filename):
            d = case(f"source-bad-{mode}", report(), LINKED)
            (d / "data" / filename).write_bytes(b"bad\n")
            assert call(d, mode, *values)["RESULT"] == STORAGE
            assert not (d / "data/maintenance.txt").exists()
        check("相关文件预验证失败 " + mode, source_bad)

    def queries():
        d = case("queries", bookings=LINKED, faults=(4,))
        q = call(d,"usage",20261020,3)
        assert q["COUNT"] == 2 and q["FREE"] == 37 and q["FAULTS"] == 1
        assert q["STUDENT"] == [["U1","张三","1"],["U2","李四","2"]]
        active = call(d,"student","U1",0)
        assert active["IDENTITY"] == "U1,张三" and active["ACTIVE"] == 4 and active["HISTORY"] == 4
        assert call(d,"student","U",1)["RESULT"] == MISSING
        assert call(d,"student","U3",1)["COUNT"] == 0
        assert call(d,"usage",20261021,0)["COUNT"] == 0
        assert call(d,"usage",20260230,0)["RESULT"] == INVALID
        assert call(d,"usage",20261020,9)["RESULT"] == INVALID
        assert call(d,"process",999,1)["RESULT"] == NOT_FOUND
    check("管理员真实预约/姓名/人数/精确学号/空表/参数边界", queries)
    def history():
        d=case("history",bookings=reservation(1,status=0)+reservation(2,date=20261021))
        q=call(d,"student","U1",0)
        assert q["ACTIVE"]==1 and q["HISTORY"]==2 and q["COUNT"]==1
        assert call(d,"student","U1",1)["COUNT"]==2
    check("学生查询有效与全部历史计数/取消保留",history)
    def orphan():
        d=case("orphan",bookings=reservation(1,student="Missing"))
        assert call(d,"usage",20261020,3)["RESULT"]==MISSING
    check("预约引用缺失账户明确报错",orphan)

    for stage in [1,2,3,4]:
        for kind in ["write","flush","close"]:
            def io_failure(stage=stage,kind=kind):
                d=case(f"io-{stage}-{kind}",report(),LINKED)
                q=call(d,"process",1,1,env={"LAB_TEST_STAGE":str(stage),"LAB_TEST_IO_FAILURE":kind})
                assert q["INJECTED"]==1 and q["RESULT"]==(STORAGE if stage==1 else RECOVERY)
                assert call(d,"reports",0,1)["RESULT"]==(OK if stage==1 else RECOVERY)
                assert (d / "data/error.txt").read_bytes()==report()
                if stage==1:
                    assert statuses(d)[3]==1 and not (d / "data/maintenance.txt").exists()
                else:
                    assert call(d,"save-gates")["RESULT"]==OK
                    assert call(d,"booking",3,who="U3")["BOOKING"]==12
                    assert call(d,"recover",env={"LAB_TEST_NOW":"20261025,16,0,0"})["RESULT"]==OK
                    assert statuses(d)=={1:1,2:1,3:0,4:0,5:1}
                    assert call(d,"reports",0,1)["COUNT"]==1
                    assert not (d / "data/maintenance.txt").exists()
                    assert (d / "data/maintenance.done.txt").is_file()
                    target_snapshot={p.name:(p.read_bytes(),p.stat().st_mtime_ns) for p in (d / "data").iterdir() if p.is_file()}
                    assert call(d,"recover")["RESULT"]==OK
                    assert target_snapshot=={p.name:(p.read_bytes(),p.stat().st_mtime_ns) for p in (d / "data").iterdir() if p.is_file()}
            check(f"真实{kind}失败 第{stage}快照/门禁/冻结时刻恢复/幂等",io_failure)

    for stage in [2,3,4]:
        def crash(stage=stage):
            d=case(f"crash-{stage}",report(),LINKED)
            call(d,"process",1,1,env={"LAB_TEST_STAGE":str(stage),"LAB_TEST_IO_FAILURE":"crash"},exit_code=86)
            assert (d / "data/maintenance.txt").is_file()
            assert (d / "data/error.txt").read_bytes()==report()
            if stage==3:
                assert (d / "data/computers.txt").read_bytes().startswith(b"1|1")
                assert statuses(d)[3]==1  # 真正处于两个保存之间。
            if stage==4:
                assert statuses(d)[3]==0  # 报告写入晚于电脑和预约。
            r=run(d,[str(executable)],lines(4))
            assert b"maintenance" not in r.stderr and statuses(d)[3]==0 and statuses(d)[4]==0
            assert call(d,"reports",0,1)["COUNT"]==1
            snap={p.name:p.read_bytes() for p in (d / "data").iterdir() if p.is_file()}
            run(d,[str(executable)],lines(4))
            assert snap=={p.name:p.read_bytes() for p in (d / "data").iterdir() if p.is_file()}
        check(f"真实进程_Exit 第{stage}快照/正式程序重启恢复",crash)

    def unrecoverable():
        d=case("unrecoverable",report(),LINKED)
        call(d,"process",1,1,env={"LAB_TEST_STAGE":"3","LAB_TEST_IO_FAILURE":"crash"},exit_code=86)
        (d / "data/reservations.txt").write_bytes(b"bad\n")
        snapshot={p.name:p.read_bytes() for p in (d / "data").iterdir() if p.is_file()}
        assert call(d,"recover")["RESULT"]==RECOVERY
        assert call(d,"save-gates")["RESULT"]==OK
        assert call(d,"submit",2,"测试",who="U1")["RESULT"]==RECOVERY
        run(d,[str(executable)],lines(4),exit_code=1)
        assert snapshot=={p.name:p.read_bytes() for p in (d / "data").iterdir() if p.is_file()}
    check("恢复源文件损坏/启动失败/全部保存门禁/保留证据",unrecoverable)

    for i,journal in enumerate([b"bad\n",b"P3|1|1|1|0|20260230|10|0|0\n",b"P3|1|3|1|0|20261019|10|0|0\n",b"P3|1|1|1|0|20261019|10|0|0\nextra\n"]):
        def bad_journal(i=i,journal=journal):
            d=case(f"journal-{i}")
            (d / "data/maintenance.txt").write_bytes(journal)
            assert call(d,"recover")["RESULT"]==RECOVERY
            run(d,[str(executable)],lines(4),exit_code=1)
            assert call(d,"save-gates")["RESULT"]==OK
        check(f"损坏维护日志拒绝恢复 {i}",bad_journal)

    for filename,mode,values in [("error.txt.tmp","submit",(2,"测试")),("computers.txt.tmp","change",(1,1)),
                                  ("reservations.txt.tmp","change",(1,1)),("maintenance.txt.tmp","change",(1,1))]:
        def blocked_temp(filename=filename,mode=mode,values=values):
            d=case("temp-"+filename,report(),LINKED)
            (d / "data" / filename).mkdir()
            assert call(d,mode,*values,who="U1" if mode=="submit" else "admin")["RESULT"]==STORAGE
            assert not (d / "data/maintenance.txt").exists()
            assert statuses(d)[3]==1
        check("遗留临时路径不覆盖 " + filename,blocked_temp)

    for mode,values,who in [("submit",(2,"测试"),"U1"),("load",(),"admin"),("usage",(20261020,3),"admin"),
                            ("student",("U1",1),"admin"),("preview",(1,1),"admin"),("process",(1,1),"admin")]:
        def memory_sweep(mode=mode,values=values,who=who):
            d=case("alloc-count-"+mode,report(),LINKED)
            total=call(d,mode,*values,who=who)["ALLOCATIONS"]
            assert total>0
            for nth in range(1,total+1):
                d=case(f"alloc-{mode}-{nth}",report(),LINKED)
                q=call(d,mode,*values,who=who,env={"LAB_TEST_FAIL_ALLOC":str(nth)})
                assert q["RESULT"] in [MEMORY,STORAGE,RECOVERY]
                if (d / "data/maintenance.txt").exists():
                    assert call(d,"recover")["RESULT"]==OK
                    assert statuses(d)[3]==0
        check("每次动态分配失败输出/释放/恢复 " + mode,memory_sweep)

    def ui_student():
        d=case("ui-student")
        r=run(d,[str(ui)],lines(2,2,"U1",PASSWORD,5,1,"中文键盘故障",1,7,3,4))
        assert "上报成功，报告编号：1".encode() in r.stdout
        assert "U1|1|20261019|中文键盘故障|0" in (d / "data/error.txt").read_text(encoding="utf-8")
    check("UI学生完整上报/确认/中文说明",ui_student)
    def ui_admin():
        d=case("ui-admin",report(),LINKED)
        actions=[2,1,4,2,3,1,4,0,5,1,1,1,0,1,3,20261020,3,4,"U1",1,5,0,6,3,3,4]
        r=run(d,[str(ui)],lines(1,2,"Admin",PASSWORD,*actions))
        for text in ["报告处理成功","预约人数不代表签到","姓名：张三","历史预约总数","学生菜单6按预约记录实时查询","实验室统计"]:
            assert text.encode() in r.stdout
        assert b"01[!]" in r.stdout and statuses(d)[3]==0
        assert "登录状态已清除".encode() in r.stdout
    check("UI维护/报告过滤处理/真实查询/公共座位刷新/P4统计入口",ui_admin)
    for i,actions in enumerate([[5], [5,1], [5,1,"说明"], [5,1,"x"*256], [5,1,"汉"*86], [5,"abc"], [5,1,"说明",0,7,3,4]]):
        def ui_input(i=i,actions=actions):
            d=case(f"ui-input-{i}")
            r=run(d,[str(ui)],lines(2,2,"U1",PASSWORD,*actions))
            assert (d / "data/error.txt").read_bytes()==b""
            assert "上报成功".encode() not in r.stdout
        check(f"UI EOF/超长/非数字/放弃 {i}",ui_input)
    def ui_read_fail():
        d=case("ui-read-fail",bookings=b"bad\n")
        r=run(d,[str(ui)],lines(1,2,"Admin",PASSWORD,3,20261020,3,6,3,4))
        assert "查询失败".encode() in r.stdout and "暂无有效预约".encode() not in r.stdout
    check("UI读取失败不伪装零预约",ui_read_fail)
    for kind in ["write", "flush", "close"]:
        def submit_io(kind=kind):
            d=case("submit-io-"+kind,report())
            q=call(d,"submit",2,"测试",who="U1",env={"LAB_TEST_IO_FAILURE":kind})
            assert q["RESULT"]==STORAGE and q["INJECTED"]==1
            assert (d / "data/error.txt").read_bytes()==report()
            assert call(d,"reports",0,-1)["COUNT"]==1
        check("上报真实I/O失败保留旧报告 " + kind,submit_io)

    if os.name=="nt":
        import ctypes
        from ctypes import wintypes
        kernel=ctypes.WinDLL("kernel32",use_last_error=True)
        kernel.CreateFileW.argtypes=[wintypes.LPCWSTR,wintypes.DWORD,wintypes.DWORD,wintypes.LPVOID,wintypes.DWORD,wintypes.DWORD,wintypes.HANDLE]
        kernel.CreateFileW.restype=wintypes.HANDLE
        kernel.CloseHandle.argtypes=[wintypes.HANDLE]
        kernel.CloseHandle.restype=wintypes.BOOL
        for filename in ["computers.txt","reservations.txt","error.txt","maintenance.done.txt"]:
            def replace_failure(filename=filename):
                d=case("replace-"+filename,report(),LINKED)
                path=d / "data" / filename
                if filename=="maintenance.done.txt":
                    path.write_bytes(b"previous completed operation\n")
                handle=kernel.CreateFileW(str(path),0x80000000,3,None,3,0x80,None)
                assert handle!=ctypes.c_void_p(-1).value
                try:
                    q=call(d,"process",1,1)
                    assert q["RESULT"]==RECOVERY
                    assert call(d,"recover")["RESULT"]==RECOVERY
                finally:
                    assert kernel.CloseHandle(handle)
                assert (d / "data/maintenance.txt").is_file()
                assert call(d,"recover")["RESULT"]==OK
                assert call(d,"reports",0,1)["COUNT"]==1 and statuses(d)[3]==0
            check("真实Windows替换失败/恢复重试 " + filename,replace_failure)
        def report_read_failure():
            d=case("report-read-locked",report())
            path=d / "data/error.txt"
            handle=kernel.CreateFileW(str(path),0x80000000,0,None,3,0x80,None)
            assert handle!=ctypes.c_void_p(-1).value
            try:
                assert call(d,"reports",0,-1)["RESULT"]==STORAGE
                assert call(d,"submit",2,"测试",who="U1")["RESULT"]==STORAGE
            finally:
                assert kernel.CloseHandle(handle)
        check("真实Windows报告读取失败不当空表",report_read_failure)

    def missing_recovery_source():
        d=case("missing-recovery-source",report(),LINKED)
        call(d,"process",1,1,env={"LAB_TEST_STAGE":"3","LAB_TEST_IO_FAILURE":"crash"},exit_code=86)
        path=d / "data/reservations.txt"
        path.rename(d / "data/reservations.absent-backup.txt")
        run(d,[str(executable)],lines(4),exit_code=1)
        assert not path.exists()  # 恢复必须先于启动补建文件。
        assert (d / "data/maintenance.txt").is_file()
        (d / "data/reservations.absent-backup.txt").rename(path)
        assert call(d,"recover")["RESULT"]==OK and statuses(d)[3]==0
    check("恢复源缺失时禁止启动补建/修复源后继续恢复",missing_recovery_source)

    def confirmation_time():
        d=case("confirmation-time",bookings=LINKED)
        assert call(d,"change",1,1,env={"LAB_TEST_NOW":"20261019,10,59,59","LAB_TEST_LATER":"20261019,11,0,0"})["RESULT"]==OK
        assert statuses(d)[3]==1 and statuses(d)[4]==0
        assert "|20261019|11|0|0" in (d / "data/maintenance.done.txt").read_text()
    check("最终确认时钟重查/日志冻结新边界",confirmation_time)

    def data_untouched():
        assert original=={p.name:p.read_bytes() for p in (root / "data").iterdir() if p.is_file()}
    check("真实开发数据完全不变/所有用例保护账户及累计时间",data_untouched)

    passed=sum(r["passed"] for r in results)
    document={"phase":"P3","passed":passed,"total":len(results),"results":results}
    (run_root / "results.json").write_text(json.dumps(document,ensure_ascii=False,indent=2),encoding="utf-8")
    report_text=f"# P3 实际执行结果\n\n通过：{passed}/{len(results)}\n\n"+"\n".join(f"- {'PASS' if r['passed'] else 'FAIL'} {r['name']} {r['detail']}".rstrip() for r in results)+"\n"
    assert PASSWORD not in report_text
    (run_root / "TEST_RESULTS.md").write_text(report_text,encoding="utf-8")
    print(f"\n{passed}/{len(results)} passed; report: {run_root}")
    return int(passed!=len(results))


if __name__=="__main__":
    raise SystemExit(main())
