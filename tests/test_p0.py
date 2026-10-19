"""P0 黑盒与存储集成测试；所有测试数据保留，不执行批量或递归删除。"""
import argparse
import ctypes
import datetime
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import uuid


def main():
    """解析命令行、构建驱动并运行独立用例；返回进程退出码。"""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", default="smart-lab.exe")
    parser.add_argument("--cc", default="gcc")
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    executable = Path(args.executable).resolve()
    compiler = shutil.which(args.cc)
    if not compiler or not executable.is_file():
        parser.error("需要已构建的程序及可用 GCC；可用 --cc 指定完整路径。")
    run_root = root / "tests" / "runs" / (
        datetime.datetime.now().strftime("%Y%m%d-%H%M%S-") + uuid.uuid4().hex[:8]
    )
    run_root.mkdir(parents=True)
    probe = run_root / "storage-probe.exe"
    build_command = [compiler, "-std=c11", "-Wall", "-Wextra", "-Wpedantic", "-Werror",
                     "-I", str(root / "include"), str(root / "tests/storage_probe.c"),
                     str(root / "src/storage.c"), str(root / "src/lab.c"),
                     str(root / "src/utils.c"), "-o", str(probe)]
    built = subprocess.run(build_command, capture_output=True, timeout=60)
    (run_root / "probe-build.log").write_bytes(built.stdout + built.stderr)
    if built.returncode != 0:
        print(built.stderr.decode("utf-8", errors="replace"))
        return 1
    records = []
    filenames = ["admins.txt", "students.txt", "computers.txt", "reservations.txt",
                 "error.txt", "count.txt"]
    default_bytes = b"".join(f"{i}|0\n".encode() for i in range(1, 41))

    def case(name):
        """为单个用例创建唯一工作目录；参数 name 为用例名，返回目录路径。"""
        directory = run_root / name
        directory.mkdir()
        return directory

    def run(directory, data=b"0\n", *, tool=executable, arguments=()):
        """在给定目录运行程序并保存 stdout/stderr；返回 CompletedProcess。"""
        result = subprocess.run([str(tool), *arguments], input=data, cwd=directory,
                                capture_output=True, timeout=5)
        index = len(list(directory.glob("stdout-*.txt"))) + 1
        (directory / f"stdout-{index}.txt").write_bytes(result.stdout)
        (directory / f"stderr-{index}.txt").write_bytes(result.stderr)
        return result

    def seed(directory, content=default_bytes):
        """写入用例指定电脑文件；参数 content 为测试字节；返回电脑文件路径。"""
        data_dir = directory / "data"
        data_dir.mkdir()
        path = data_dir / "computers.txt"
        path.write_bytes(content)
        return path

    def verify(name, test):
        """运行一个测试并记录失败；参数 name 名称、test 无参函数；返回无。"""
        try:
            test()
            records.append({"name": name, "passed": True, "detail": ""})
            print("PASS", name)
        except Exception as error:
            records.append({"name": name, "passed": False,
                            "detail": f"{type(error).__name__}: {error}"})
            print("FAIL", name, records[-1]["detail"])

    def first_start():
        """验证目录和六个文件自动生成、40 台正常电脑及 5×8 座位。"""
        directory = case("first-start")
        result = run(directory, b"3\n0\n")
        assert result.returncode == 0 and not result.stderr, result.stderr
        assert "智能实验室预约系统" in result.stdout.decode("utf-8")
        assert sorted(p.name for p in (directory / "data").iterdir()) == sorted(filenames)
        assert (directory / "data/computers.txt").read_bytes() == default_bytes
        seats = re.findall(rb"(\d{2})\[([*!#])\]", result.stdout)
        assert seats == [(f"{i:02d}".encode(), b"*") for i in range(1, 41)]
        rows = [line for line in result.stdout.splitlines() if re.match(rb"\d{2}\[", line)]
        assert len(rows) == 5 and all(len(re.findall(rb"\d{2}\[", row)) == 8 for row in rows)
        assert all(len(row) == 47 for row in rows)

    verify("首次启动/六文件/40台/5×8布局/正常退出", first_start)

    def restart():
        """已有乱序 CRLF 数据和非电脑文件必须逐字节保持，故障座位正确恢复。"""
        directory = case("restart")
        content = b"".join(f"{i}|{int(i == 7)}\r\n".encode() for i in range(40, 0, -1))
        path = seed(directory, content)
        for name in filenames:
            if name != "computers.txt":
                (directory / "data" / name).write_bytes(("保留测试|" + name).encode())
        before = {p.name: p.read_bytes() for p in (directory / "data").iterdir()}
        timestamp = path.stat().st_mtime_ns
        for _ in range(2):
            result = run(directory, b"3\n0\n")
            assert result.returncode == 0 and b"07[!]" in result.stdout
            seats = re.findall(rb"(\d{2})\[([*!#])\]", result.stdout)
            assert [int(number) for number, _ in seats] == list(range(1, 41))
        assert before == {p.name: p.read_bytes() for p in (directory / "data").iterdir()}
        assert path.stat().st_mtime_ns == timestamp

    verify("重启/故障恢复/乱序CRLF/已有六文件不覆盖", restart)

    invalid_inputs = {
        # P1 明确规定 4 退出，因此越界输入改为 5，仍验证拒绝/恢复/座位数量。
        "letter": b"abc\n", "negative": b"-1\n", "out-of-range": b"5\n",
        "large-int": b"999999999999999999999999999999999\n", "empty": b"\n",
        "spaces": b" \t\r\n", "long-line": b"3" * 10000 + b"\n",
        "trailing-text": b"3abc\n", "decimal": b"1.5\n", "nul": b"3\x00\n",
        "sign": b"+1\n", "two-numbers": b"1 2\n",
    }
    for name, data in invalid_inputs.items():
        def invalid_menu(name=name, data=data):
            """非法行只报一次错误，余下行被正确消费并可查看座位和退出。"""
            directory = case("input-" + name)
            result = run(directory, data + b"3\n0\n")
            assert result.returncode == 0 and not result.stderr
            assert result.stdout.count("输入无效".encode()) == 1
            assert len(re.findall(rb"\d{2}\[[*!]\]", result.stdout)) == 40
        verify("菜单非法输入：" + name, invalid_menu)

    for name, data in {"immediate": b"", "no-newline": b"0", "after-invalid": b"abc",
                       "after-long": b"9" * 10000, "after-seat": b"3\n"}.items():
        def eof_menu(name=name, data=data):
            """输入结束（含未带换行的末行）必须及时正常退出。"""
            result = run(case("eof-" + name), data)
            assert result.returncode == 0 and not result.stderr
            assert any(text.encode() in result.stdout for text in ["再见", "输入结束"])
        verify("EOF：" + name, eof_menu)

    def role_entries():
        """P1 已实现入口：验证实际角色选项、返回和首尾空白，不能继续要求占位提示。"""
        result = run(case("role-entries"), b" 1 \r\n3\n2\n3\n 0 \n")
        assert result.returncode == 0 and not result.stderr
        assert "1. 管理员注册".encode() in result.stdout
        assert "2. 管理员登录".encode() in result.stdout
        assert "1. 学生注册".encode() in result.stdout
        assert "2. 学生登录".encode() in result.stdout
        assert result.stdout.count("3. 返回主菜单".encode()) == 2
        assert "感谢使用，再见".encode() in result.stdout
    verify("管理员/学生实际入口及合法空白", role_entries)

    def recovery():
        """模拟数据目录缺失：移到明确备份路径（不删除），然后重新启动。"""
        directory = case("missing-directory-recovery")
        assert run(directory).returncode == 0
        (directory / "data").rename(directory / "data-before-recovery")
        assert run(directory).returncode == 0
        assert (directory / "data/computers.txt").read_bytes() == default_bytes
        assert (directory / "data-before-recovery/computers.txt").read_bytes() == default_bytes
    verify("缺失数据目录后恢复（旧目录保留备份）", recovery)

    def empty_file():
        """零字节电脑文件允许初始化。"""
        directory = case("empty-computers")
        path = seed(directory, b"")
        assert run(directory).returncode == 0
        assert path.read_bytes() == default_bytes
    verify("零字节电脑文件初始化", empty_file)

    corruptions = {
        "garbage": b"broken\n", "blank-only": b"\n", "spaces-only": b" \r\n",
        "missing-record": default_bytes.replace(b"40|0\n", b""),
        "duplicate": default_bytes.replace(b"40|0", b"1|0"),
        "bad-number": default_bytes.replace(b"40|0", b"41|0"),
        "zero-number": default_bytes.replace(b"40|0", b"0|0"),
        "negative-number": default_bytes.replace(b"40|0", b"-1|0"),
        "bad-status": default_bytes.replace(b"40|0", b"40|2"),
        "extra-record": default_bytes + b"40|0\n",
        "extra-field": default_bytes.replace(b"40|0", b"40|0|x"),
        "suffix": default_bytes.replace(b"40|0", b"40|0x"),
        "overflow": default_bytes.replace(b"40|0", b"99999999999999999999999999|0"),
        "nul": default_bytes.replace(b"40|0", b"40|0\x00x"),
        "long-line": b"1" * 10000 + b"\n" + default_bytes,
        "utf8-bom": b"\xef\xbb\xbf" + default_bytes,
        "invalid-utf8": b"\xff|0\n" + default_bytes,
        "trailing-blank": default_bytes + b"\n",
    }
    for name, content in corruptions.items():
        def corruption(name=name, content=content):
            """损坏电脑文件必须报错退出，原文件不变且不生成替换快照。"""
            directory = case("corrupt-" + name)
            path = seed(directory, content)
            timestamp = path.stat().st_mtime_ns
            result = run(directory)
            assert result.returncode != 0 and result.stderr
            assert path.read_bytes() == content and path.stat().st_mtime_ns == timestamp
            assert not (directory / "data/computers.txt.tmp").exists()
        verify("损坏保留原文件：" + name, corruption)

    def no_last_newline():
        """完整文件最后一行可没有换行。"""
        directory = case("no-last-newline")
        path = seed(directory, default_bytes.rstrip(b"\n"))
        assert run(directory).returncode == 0
        assert path.read_bytes() == default_bytes.rstrip(b"\n")
    verify("电脑文件末行无换行", no_last_newline)

    def missing_side_file():
        """已有有效电脑时其他缺失文件应补齐，电脑状态保持。"""
        directory = case("missing-side-files")
        content = default_bytes.replace(b"7|0\n", b"7|1\n")
        path = seed(directory, content)
        assert run(directory).returncode == 0
        assert len(list((directory / "data").iterdir())) == 6 and path.read_bytes() == content
    verify("补齐缺失业务文件并保留电脑", missing_side_file)

    def directory_is_file():
        """data 同名普通文件应明确报错并保留。"""
        directory = case("directory-is-file")
        path = directory / "data"
        path.write_bytes(b"keep")
        result = run(directory)
        assert result.returncode != 0 and result.stderr and path.read_bytes() == b"keep"
    verify("数据目录被同名文件占用", directory_is_file)

    for name in filenames:
        def file_is_directory(name=name):
            """每个数据文件若被目录占用，应报错且不替换该目录。"""
            directory = case("file-is-directory-" + name)
            (directory / "data" / name).mkdir(parents=True)
            result = run(directory)
            assert result.returncode != 0 and result.stderr
            assert (directory / "data" / name).is_dir()
        verify("数据文件类型错误：" + name, file_is_directory)

    def save_round_trip():
        """存储接口实际保存故障状态，再经独立进程重新读取。"""
        directory = case("save-round-trip")
        assert run(directory, tool=probe, arguments=("save",)).returncode == 0
        result = run(directory, b"3\n0\n")
        assert result.returncode == 0 and b"07[!]" in result.stdout
        assert not (directory / "data/computers.txt.tmp").exists()
    verify("保存接口/临时替换/故障状态跨进程持久化", save_round_trip)

    for mode in ["invalid-no", "duplicate", "invalid-status", "null"]:
        def invalid_save(mode=mode):
            """无效内存快照不应改变磁盘数据。"""
            directory = case("save-" + mode)
            path = seed(directory)
            result = run(directory, tool=probe, arguments=(mode,))
            assert result.returncode == 0 and result.stderr
            assert path.read_bytes() == default_bytes
            assert not (directory / "data/computers.txt.tmp").exists()
        verify("保存拒绝无效快照：" + mode, invalid_save)

    def stale_snapshot():
        """遗留 .tmp 不允许被覆盖，原目标与临时字节均保留。"""
        directory = case("stale-snapshot")
        path = seed(directory)
        temporary = directory / "data/computers.txt.tmp"
        temporary.write_bytes(b"unfinished-keep")
        result = run(directory, tool=probe, arguments=("save",))
        assert result.returncode != 0 and result.stderr
        assert path.read_bytes() == default_bytes
        assert temporary.read_bytes() == b"unfinished-keep"
    verify("临时文件独占创建/遗留文件不覆盖", stale_snapshot)

    def temporary_is_directory():
        """临时文件无法创建时必须保留原电脑文件。"""
        directory = case("temporary-is-directory")
        path = seed(directory)
        (directory / "data/computers.txt.tmp").mkdir()
        result = run(directory, tool=probe, arguments=("save",))
        assert result.returncode != 0 and result.stderr
        assert path.read_bytes() == default_bytes
    verify("临时文件创建失败不破坏旧数据", temporary_is_directory)

    if os.name == "nt":
        def replacement_denied():
            """Windows 持有禁止删除共享的读句柄，实际触发最终替换失败。"""
            from ctypes import wintypes
            directory = case("replacement-denied")
            path = seed(directory)
            kernel = ctypes.WinDLL("kernel32", use_last_error=True)
            kernel.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD,
                                          wintypes.LPVOID, wintypes.DWORD, wintypes.DWORD,
                                          wintypes.HANDLE]
            kernel.CreateFileW.restype = wintypes.HANDLE
            kernel.CloseHandle.argtypes = [wintypes.HANDLE]
            kernel.CloseHandle.restype = wintypes.BOOL
            handle = kernel.CreateFileW(str(path), 0x80000000, 0x3, None, 3, 0x80, None)
            assert handle != ctypes.c_void_p(-1).value, ctypes.get_last_error()
            try:
                result = run(directory, tool=probe, arguments=("save",))
                assert result.returncode != 0 and "替换".encode() in result.stderr
                assert path.read_bytes() == default_bytes
                assert b"7|1\n" in (directory / "data/computers.txt.tmp").read_bytes()
            finally:
                assert kernel.CloseHandle(handle)
        verify("Windows替换失败保留原文件和临时快照", replacement_denied)

    failures = sum(not record["passed"] for record in records)
    report = {"platform": sys.platform, "executable": str(executable),
              "compiler": compiler, "probe_build_command": build_command,
              "total": len(records), "passed": len(records) - failures,
              "failed": failures, "cases": records}
    (run_root / "results.json").write_text(json.dumps(report, ensure_ascii=False, indent=2),
                                           encoding="utf-8")
    lines = ["# P0 自动测试记录", "", f"平台：{sys.platform}",
             f"总计 {len(records)}；通过 {len(records) - failures}；失败 {failures}。", "",
             "测试数据、构建日志和进程输出保留在本文件同目录，不自动删除。", "",
             "| 测试 | 结果 | 失败详情 |", "|---|---|---|"]
    lines.extend(f"| {r['name']} | {'通过' if r['passed'] else '失败'} | {r['detail']} |"
                 for r in records)
    (run_root / "TEST_RESULTS.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"\n{len(records) - failures}/{len(records)} passed; report: {run_root}")
    return int(failures != 0)


if __name__ == "__main__":
    raise SystemExit(main())
