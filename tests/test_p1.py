"""P1 黑盒/API 测试；仅使用隔离目录，不清理数据，不记录密码输入或账户原文。"""
import argparse
import ctypes
import datetime
import json
import os
from pathlib import Path
import shutil
import subprocess
import uuid


PASSWORD = "CourseOnly123!"  # 仅合成测试数据；报告和进程日志不得包含此值。
WRONG = "DifferentValue!"


def lines(*values):
    """参数为测试输入行（字节或文本），返回含末尾换行的输入，不用于日志。"""
    return b"\n".join(v if isinstance(v, bytes) else str(v).encode() for v in values) + b"\n"


def main():
    """构建 API 驱动、执行隔离用例并输出脱敏报告；返回失败状态。"""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", default="smart-lab.exe")
    parser.add_argument("--cc", default="gcc")
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    executable = Path(args.executable).resolve()
    compiler = shutil.which(args.cc)
    if not compiler or not executable.is_file():
        parser.error("需要可运行程序和 GCC。")
    run_root = root / "tests/runs" / (
        "p1-" + datetime.datetime.now().strftime("%Y%m%d-%H%M%S-") + uuid.uuid4().hex[:8])
    run_root.mkdir(parents=True)
    root_data = {p.name: p.read_bytes() for p in (root / "data").iterdir() if p.is_file()}
    probe = run_root / "auth-probe.exe"
    command = [compiler, "-std=c11", "-Wall", "-Wextra", "-Wpedantic", "-Werror",
               "-I", str(root / "include"), str(root / "tests/auth_probe.c"),
               *[str(root / "src" / f"{name}.c") for name in ["auth", "storage", "utils", "lab"]],
               "-o", str(probe)]
    built = subprocess.run(command, capture_output=True, timeout=60)
    (run_root / "probe-build.log").write_bytes(built.stdout + built.stderr)
    if built.returncode:
        print("API driver build failed; see probe-build.log")
        return 1
    records = []

    def case(name):
        """参数 name 为安全用例名；返回新建的独立目录。"""
        directory = run_root / name
        directory.mkdir()
        return directory

    def run(directory, data=b"4\n", mode=None, role=1):
        """在隔离目录运行 UI 或 API 驱动，返回进程结果；不记录 stdin。"""
        invocation = [str(executable)] if mode is None else [str(probe), "admin" if role == 1 else "student", mode]
        result = subprocess.run(invocation, input=data, cwd=directory, capture_output=True, timeout=10)
        # 日志禁止凭据，校验后才写入；错误报告也不复述输入或断言值。
        if any(secret.encode() in result.stdout + result.stderr
               for secret in [PASSWORD, WRONG, "Q" * 63, "课程测试🧪"]):
            raise AssertionError("credential output blocked")
        index = len(list(directory.glob("stdout-*.txt"))) + 1
        (directory / f"stdout-{index}.txt").write_bytes(result.stdout)
        (directory / f"stderr-{index}.txt").write_bytes(result.stderr)
        return result

    def verify(name, function):
        """参数为安全名称和用例函数；记录状态，异常只记录类别，防止泄露凭据。"""
        try:
            function()
            records.append({"name": name, "passed": True, "detail": ""})
            print("PASS", name)
        except Exception as error:
            records.append({"name": name, "passed": False, "detail": type(error).__name__})
            print("FAIL", name, type(error).__name__)

    def account_file(directory, role):
        """参数为隔离目录和角色；返回对应账户文件路径。"""
        return directory / "data" / ("admins.txt" if role == 1 else "students.txt")

    def register(role, identifier="User1", name="张同学", password=PASSWORD, confirm=None):
        """返回完整注册并返回主菜单退出的测试输入，不打印。"""
        return lines(role, 1, identifier, name, password, password if confirm is None else confirm, 3, 4)

    def login(role, identifier="User1", password=PASSWORD, menu=()):
        """返回登录、执行指定角色操作、退出登录和系统的输入。"""
        return lines(role, 2, identifier, password, *menu, 6 if role == 1 else 7, 3, 4)

    def seed(directory, role, content):
        """参数为目录、角色与原始账户字节；初始化目录后仅写测试文件。"""
        assert run(directory).returncode == 0
        account_file(directory, role).write_bytes(content)

    for role in [1, 2]:
        label = "admin" if role == 1 else "student"

        def happy(role=role, label=label):
            """注册、跨进程重启登录、角色界面、座位与未开放功能均实际验证。"""
            directory = case(label + "-happy")
            result = run(directory, register(role))
            assert result.returncode == 0 and not result.stderr
            assert "注册成功".encode() in result.stdout
            fields = account_file(directory, role).read_text(encoding="utf-8").strip().split("|")
            assert fields[:2] == ["User1", "张同学"] and fields[2] == PASSWORD
            assert len(fields) == (3 if role == 1 else 4)
            if role == 2:
                assert fields[3] == "0"
            content = account_file(directory, role).read_bytes()
            timestamp = account_file(directory, role).stat().st_mtime_ns
            # P3 开放管理员2～4、学生5；进入新流程并返回/放弃，保留原持久化和角色断言。
            options = [1, 2, 0, 3, 20000101, 0, 4, "Missing", 5] if role == 1 else [1, 20000101, 0, 2, 20000101, 0, 3, 0, 4, 5, 1, "课程报障", 0, 6]
            result = run(directory, login(role, menu=options))
            assert result.returncode == 0 and not result.stderr
            assert "登录成功".encode() in result.stdout
            expected = "------ 管理员菜单 ------" if role == 1 else "------ 学生菜单 ------"
            other = "------ 学生菜单 ------" if role == 1 else "------ 管理员菜单 ------"
            assert expected.encode() in result.stdout and other.encode() not in result.stdout
            assert b"01[*]" in result.stdout and b"40[*]" in result.stdout
            assert result.stdout.count("该功能尚未开放".encode()) == 1
            if role == 2:
                assert "时段已经开始或日期已过去".encode() in result.stdout
                assert "我的有效预约".encode() in result.stdout and "可取消预约".encode() in result.stdout
            assert "登录状态已清除".encode() in result.stdout
            assert content == account_file(directory, role).read_bytes()
            assert timestamp == account_file(directory, role).stat().st_mtime_ns
            for name in ["reservations.txt", "error.txt", "count.txt"]:
                assert (directory / "data" / name).read_bytes() == b""
        verify(label + ": 注册/重启登录/角色菜单/退出/未开放功能", happy)

        def multiple(role=role, label=label):
            """同角色多账户注册不丢失已有记录，重复注册不改文件。"""
            directory = case(label + "-multiple")
            for identifier in ["User1", "User2", "User3"]:
                result = run(directory, register(role, identifier))
                assert "注册成功".encode() in result.stdout
            path = account_file(directory, role)
            before = path.read_bytes()
            assert len(before.splitlines()) == 3
            result = run(directory, register(role, "User2"))
            assert "已存在".encode() in result.stdout and "注册成功".encode() not in result.stdout
            assert before == path.read_bytes()
            result = run(directory, login(role, "User3"))
            assert "登录成功".encode() in result.stdout
        verify(label + ": 多账户/重复拒绝/旧账户保留", multiple)

        def retry(role=role, label=label):
            """错误密码和不存在账户失败，允许重新输入并成功登录。"""
            directory = case(label + "-retry-login")
            assert run(directory, register(role)).returncode == 0
            result = run(directory, lines(role, 2, "User1", WRONG, 2, "Missing", PASSWORD,
                                         2, "User1", PASSWORD, 6 if role == 1 else 7, 3, 4))
            assert result.returncode == 0 and not result.stderr
            assert "密码错误".encode() in result.stdout and "账户不存在".encode() in result.stdout
            assert result.stdout.count("登录成功".encode()) == 1
        verify(label + ": 错误密码/不存在/重试成功", retry)

        def mismatch(role=role, label=label):
            """确认不一致不得创建账户。"""
            directory = case(label + "-mismatch")
            result = run(directory, register(role, confirm=WRONG))
            assert "两次密码不一致".encode() in result.stdout
            assert "注册成功".encode() not in result.stdout and account_file(directory, role).read_bytes() == b""
        verify(label + ": 密码确认不一致", mismatch)

        for mode in ["roundtrip", "invalid-struct", "read-missing", "retry-write", "save-invalid"] + (
                ["read-locked"] if os.name == "nt" else []):
            def api(role=role, label=label, mode=mode):
                """API 检查会话字段、边界、错误类型和持久化失败恢复。"""
                result = run(case(label + "-api-" + mode), mode=mode, role=role)
                assert result.returncode == 0 and b"API checks OK" in result.stdout
            verify(label + ": API " + mode, api)

        invalid_fields = {
            "id-empty": [], "id-long": [b"A" * 32], "id-separator": ["Bad|Id"],
            "id-space": ["Bad Id"], "id-unicode": ["学号"], "id-leading-dash": ["-User"],
            "name-long": ["User1", b"N" * 64], "name-utf8-long": ["User1", "张" * 22],
            "name-empty": ["User1", ""], "name-separator": ["User1", "张|同学"],
            "name-bad-utf8": ["User1", b"\xed\xa0\x80"],
            "name-partial-utf8": ["User1", b"\xe5\xbc"],
            "name-nul": ["User1", b"Name\x00suffix"],
            "password-long": ["User1", "Name", b"Q" * 64],
            "password-empty": ["User1", "Name", ""],
            "password-delimiter": ["User1", "Name", "Bad|Value"],
            "password-huge": ["User1", "Name", b"Q" * 10000],
            "confirm-long": ["User1", "Name", PASSWORD, b"Q" * 64],
        }
        invalid_fields["id-empty"] = [""]
        for kind, fields in invalid_fields.items():
            def invalid_input(role=role, label=label, kind=kind, fields=fields):
                """字段超长/非法时取消整次注册，后续返回行仍可正常消费。"""
                directory = case(label + "-input-" + kind)
                result = run(directory, lines(role, 1, *fields, 3, 4))
                assert result.returncode == 0 and not result.stderr
                assert "输入无效".encode() in result.stdout
                assert "注册成功".encode() not in result.stdout
                assert account_file(directory, role).read_bytes() == b""
                assert "感谢使用".encode() in result.stdout
            verify(label + ": 输入拒绝 " + kind, invalid_input)

        def boundaries(role=role, label=label):
            """刚好达到标识/中文姓名/密码字节上限应成功，CRLF 也不能误拒绝。"""
            directory = case(label + "-boundaries")
            identifier, name, password = "A" * 31, "张" * 21, "Q" * 63
            payload = register(role, identifier, name, password).replace(b"\n", b"\r\n")
            result = run(directory, payload)
            assert "注册成功".encode() in result.stdout and not result.stderr
            result = run(directory, login(role, identifier, password))
            assert "登录成功".encode() in result.stdout and not result.stderr
            assert password.encode() not in result.stdout + result.stderr
        verify(label + ": 最大字节容量/中文/CRLF", boundaries)

        prefixes = [[role], [role, 1], [role, 1, "User1"], [role, 1, "User1", "Name"],
                    [role, 1, "User1", "Name", PASSWORD], [role, 2], [role, 2, "User1"]]
        for index, prefix in enumerate(prefixes):
            def eof(role=role, label=label, index=index, prefix=prefix):
                """角色入口与各字段 EOF 必须正常退出，不能创建半条账户。"""
                directory = case(f"{label}-eof-{index}")
                result = run(directory, lines(*prefix))
                assert result.returncode == 0 and not result.stderr
                assert "输入结束".encode() in result.stdout
                assert account_file(directory, role).read_bytes() == b""
            verify(f"{label}: EOF 字段 {index}", eof)

        def eof_logged_in(role=role, label=label):
            """已登录菜单 EOF 正常结束，不执行后续业务。"""
            directory = case(label + "-eof-session")
            run(directory, register(role))
            result = run(directory, lines(role, 2, "User1", PASSWORD))
            assert result.returncode == 0 and not result.stderr
            assert "登录成功".encode() in result.stdout and "输入结束".encode() in result.stdout
        verify(label + ": 登录后EOF", eof_logged_in)

        def menu_invalid(role=role, label=label):
            """角色入口和已登录菜单都拒绝非数字与越界选择并可继续退出。"""
            directory = case(label + "-menu-invalid")
            run(directory, register(role))
            result = run(directory, lines(role, "abc", 2, "User1", PASSWORD, "abc", 99,
                                         6 if role == 1 else 7, 3, 4))
            assert result.returncode == 0 and not result.stderr
            assert result.stdout.count("输入无效".encode()) == 3
            assert "登录状态已清除".encode() in result.stdout
        verify(label + ": 多级菜单非法输入", menu_invalid)

        valid = lines("User1|Name|" + PASSWORD + ("|0" if role == 2 else ""))
        bad = {
            "empty-line": b"\n", "missing-field": b"User1|Name\n", "extra-field": valid.rstrip(b"\n") + b"|x\n",
            "duplicate": valid + valid, "bad-id": valid.replace(b"User1", b"Bad Id"),
            "long-id": valid.replace(b"User1", b"A" * 32), "long-name": valid.replace(b"Name", b"A" * 64),
            "long-password": valid.replace(PASSWORD.encode(), b"Q" * 64),
            "empty-name": valid.replace(b"Name", b""), "empty-password": valid.replace(PASSWORD.encode(), b""),
            "nul": valid.replace(b"Name", b"Name\x00hidden"),
            "utf8": valid.replace(b"Name", b"\xff"), "overlong-utf8": valid.replace(b"Name", b"\xc0\xaf"),
            "surrogate": valid.replace(b"Name", b"\xed\xa0\x80"),
            "too-high-codepoint": valid.replace(b"Name", b"\xf4\x90\x80\x80"),
            "control": valid.replace(b"Name", b"Na\tme"), "bom": b"\xef\xbb\xbf" + valid,
            "huge-line": b"X" * 10000 + b"\n", "trailing-blank": valid + b"\n",
        }
        if role == 2:
            bad.update({"negative-time": valid.replace(b"|0\n", b"|-1\n"),
                        "overflow-time": valid.replace(b"|0\n", b"|999999999999999999999999\n"),
                        "text-time": valid.replace(b"|0\n", b"|one\n")})
        for kind, content in bad.items():
            def corruption(role=role, label=label, kind=kind, content=content):
                """损坏时 API 返回存储错误且输出不变，UI 不冒充账户不存在，不改源文件。"""
                directory = case(label + "-corrupt-" + kind)
                seed(directory, role, content)
                path = account_file(directory, role)
                timestamp = path.stat().st_mtime_ns
                result = run(directory, mode="corrupt", role=role)
                assert result.returncode == 0 and result.stderr
                result = run(directory, lines(role, 2, "User1", PASSWORD, 3, 4))
                assert result.returncode == 0 and "账户文件".encode() in result.stdout and result.stderr
                assert "账户不存在".encode() not in result.stdout and "登录成功".encode() not in result.stdout
                assert path.read_bytes() == content and path.stat().st_mtime_ns == timestamp
                assert not path.with_suffix(".txt.tmp").exists()
            verify(label + ": 损坏安全报错 " + kind, corruption)

        for extra in [0, 1]:
            def capacity(role=role, label=label, extra=extra):
                """恰达容量时允许登录拒绝新注册，超容量文件本身拒绝读取。"""
                directory = case(f"{label}-capacity-{extra}")
                count = (30 if role == 1 else 1000) + extra
                content = b"".join(lines(f"User{i}|Name|{PASSWORD}" + ("|0" if role == 2 else "")) for i in range(count))
                seed(directory, role, content)
                result = run(directory, mode="capacity" if extra == 0 else "corrupt", role=role)
                assert result.returncode == 0
                assert account_file(directory, role).read_bytes() == content
            verify(label + ": 容量边界 " + str(extra), capacity)

        def legacy(role=role, label=label):
            """读取架构格式 CRLF/无末尾换行；注册新账户保留原有账户和学生时长。"""
            directory = case(label + "-legacy")
            first = (f"User0|Old Name|{PASSWORD}" + ("|2147483647" if role == 2 else "")).encode()
            last = (f"User1|张老师|{PASSWORD}" + ("|12" if role == 2 else "")).encode()
            content = first + b"\r\n" + last
            seed(directory, role, content)
            assert run(directory, mode="login", role=role).returncode == 0
            assert account_file(directory, role).read_bytes() == content
            result = run(directory, register(role, "User2"))
            assert "注册成功".encode() in result.stdout
            records_in_file = account_file(directory, role).read_bytes().splitlines()
            assert len(records_in_file) == 3 and records_in_file[:2] == [first, last]
            if role == 2:
                assert records_in_file[0].endswith(b"|2147483647")
                assert records_in_file[1].endswith(b"|12") and records_in_file[2].endswith(b"|0")
        verify(label + ": 原格式/末行无换行/旧时长保留", legacy)

        def unicode_credentials(role=role, label=label):
            """完整四字节 UTF-8 姓名和密码往返；密码不得出现在任意进程输出。"""
            directory = case(label + "-unicode-credentials")
            result = run(directory, register(role, name="张🌟同学", password="课程测试🧪"))
            assert "注册成功".encode() in result.stdout and not result.stderr
            result = run(directory, login(role, password="课程测试🧪"))
            assert "登录成功".encode() in result.stdout and not result.stderr
        verify(label + ": 完整UTF8多字节字段往返", unicode_credentials)

        for kind in ["stale", "directory"]:
            def write_failure(role=role, label=label, kind=kind):
                """无法创建快照时 UI 不报注册成功，原账户不变，登录不出现幽灵账户。"""
                directory = case(label + "-write-" + kind)
                run(directory)
                path = account_file(directory, role)
                temporary = path.with_suffix(".txt.tmp")
                if kind == "stale":
                    temporary.write_bytes(b"keep-stale")
                else:
                    temporary.mkdir()
                before = path.read_bytes()
                result = run(directory, register(role))
                assert "注册失败".encode() in result.stdout and "注册成功".encode() not in result.stdout
                assert result.stderr and path.read_bytes() == before
                if kind == "stale":
                    assert temporary.read_bytes() == b"keep-stale"
                result = run(directory, lines(role, 2, "User1", PASSWORD, 3, 4))
                assert "账户不存在".encode() in result.stdout
            verify(label + ": 写入失败不虚报 " + kind, write_failure)

        if os.name == "nt":
            def replacement_failure(role=role, label=label):
                """Windows 实际阻止目标替换，确保不是只测参数检查。"""
                from ctypes import wintypes
                directory = case(label + "-replace-denied")
                run(directory)
                path = account_file(directory, role)
                kernel = ctypes.WinDLL("kernel32", use_last_error=True)
                kernel.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD,
                                              wintypes.LPVOID, wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
                kernel.CreateFileW.restype = wintypes.HANDLE
                kernel.CloseHandle.argtypes = [wintypes.HANDLE]
                kernel.CloseHandle.restype = wintypes.BOOL
                handle = kernel.CreateFileW(str(path), 0x80000000, 3, None, 3, 0x80, None)
                assert handle != ctypes.c_void_p(-1).value
                try:
                    result = run(directory, register(role))
                    assert "注册失败".encode() in result.stdout and "注册成功".encode() not in result.stdout
                    assert "替换".encode() in result.stderr and path.read_bytes() == b""
                    assert path.with_suffix(".txt.tmp").is_file()
                finally:
                    assert kernel.CloseHandle(handle)
            verify(label + ": Windows替换失败保留旧账户", replacement_failure)

    def isolation():
        """角色交叉凭据失败；同名账户在各自命名空间用各自密码认证。"""
        directory = case("role-isolation")
        run(directory, register(1, "AdminOnly"))
        run(directory, register(2, "StudentOnly"))
        for role, identifier in [(1, "StudentOnly"), (2, "AdminOnly")]:
            result = run(directory, lines(role, 2, identifier, PASSWORD, 3, 4))
            assert "账户不存在".encode() in result.stdout and "登录成功".encode() not in result.stdout
        run(directory, register(1, "Shared", password=PASSWORD))
        run(directory, register(2, "Shared", password=WRONG))
        result = run(directory, lines(2, 2, "Shared", PASSWORD, 2, "Shared", WRONG, 7, 3, 4))
        assert "密码错误".encode() in result.stdout and result.stdout.count("登录成功".encode()) == 1
        assert "------ 管理员菜单 ------".encode() not in result.stdout
        result = run(directory, lines(1, 2, "AdminOnly", PASSWORD, 6, 3, 2, 2, "AdminOnly", PASSWORD, 3, 4))
        assert result.stdout.count("登录成功".encode()) == 1
        assert "账户不存在".encode() in result.stdout and "登录状态已清除".encode() in result.stdout
    verify("双角色隔离/同名不同凭据/退出后跨角色须重新认证", isolation)

    def unrelated_corruption():
        """学生只读取学生表；管理员表损坏不应误作为学生不存在。"""
        directory = case("independent-role-files")
        run(directory, register(2))
        account_file(directory, 1).write_bytes(b"damaged\n")
        result = run(directory, login(2))
        assert "登录成功".encode() in result.stdout and not result.stderr
    verify("登录只读取对应角色文件", unrelated_corruption)

    def real_data_preserved():
        """确认所有用例均未改变开发 data 目录。"""
        assert root_data == {p.name: p.read_bytes() for p in (root / "data").iterdir() if p.is_file()}
    verify("真实开发数据保持不变", real_data_preserved)
    failures = sum(not item["passed"] for item in records)
    report = {"platform": os.name, "compiler": compiler, "total": len(records),
              "passed": len(records) - failures, "failed": failures, "cases": records}
    (run_root / "results.json").write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    rows = ["# P1 自动测试记录", "", f"总计 {len(records)}；通过 {len(records)-failures}；失败 {failures}。",
            "", "只记录测试名称与结果。密码输入不写日志，账户数据仅存在隔离用例的 data 文件。", "",
            "| 用例 | 结果 |", "|---|---|"]
    rows.extend(f"| {r['name']} | {'通过' if r['passed'] else '失败：' + r['detail']} |" for r in records)
    (run_root / "TEST_RESULTS.md").write_text("\n".join(rows) + "\n", encoding="utf-8")
    print(f"\n{len(records)-failures}/{len(records)} passed; report: {run_root}")
    return int(failures != 0)


if __name__ == "__main__":
    raise SystemExit(main())
