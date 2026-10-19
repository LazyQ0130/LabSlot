# P0 测试与验收记录

P1 开发前的基线仍为 57/57；本轮适配新菜单后的回归也为 57/57。具体命令、原始报告与两处菜单断言调整原因见 [P1 验收记录](P1_TEST_CASES.md)。以下保留 P0 原始交付历史。

## 执行方法

从项目根目录执行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\build.ps1 -Test
# 或
mingw32-make test
```

测试脚本只使用 Python 标准库，另行编译 `storage_probe.c` 验证实际保存接口。测试目录使用唯一名称，所有生成的文件均保留，不执行批量删除。每例进程设有 5 秒超时，防止非法输入/EOF 造成死循环却被误判通过。

每次执行会生成 `tests/runs/<运行标识>/TEST_RESULTS.md`、`results.json`、构建日志，以及各用例的原始 stdout/stderr。重复运行不覆盖前一轮数据。

## 首轮实测

- 日期：2026-10-19（Asia/Shanghai）。
- 平台：Windows，原生运行 `.exe`，不是 Linux 替代验证。
- 工具链：w64devkit 2.10.0，GCC 16.2.0。
- 正式程序编译：`gcc -std=c11 -Wall -Wextra -Wpedantic -Werror -Iinclude src/main.c src/lab.c src/storage.c src/ui.c src/utils.c -o smart-lab.exe`。
- 另已执行任务建议的通配符命令：`gcc -std=c11 -Wall -Wextra -Wpedantic -Iinclude src/*.c -o smart-lab.exe`。
- 编译结果：成功，未出现警告；`-Werror` 构建也成功。
- 自动化命令：`python tests/test_p0.py --cc gcc`。
- 首轮结果：**57/57 通过，0 失败**。
- 原始详细记录：[首轮报告](results/20261019-090322-56a7364d-TEST_RESULTS.md)。

另已实际执行 `mingw32-make -B test`，构建成功且 **57/57 通过**：[Makefile 验证报告](results/20261019-090520-2b1d6168-TEST_RESULTS.md)。九个公开头文件也已一起通过 GCC 独立语法检查，包含重复引用以验证 include guard。

最终在 Windows PowerShell 5.1 下执行 `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\build.ps1 -Test`，严格构建成功且 **57/57 通过**：[最终报告](results/20261019-090541-221b9771-TEST_RESULTS.md)。

另执行 `gcc -std=c11 -Wall -Wextra -Wpedantic -Werror -fanalyzer -Iinclude src/main.c src/lab.c src/storage.c src/ui.c src/utils.c -o tests/runs/20261019-090541-221b9771/analyzed-smart-lab.exe`，成功且未报告警告。21 个源代码/配置/说明文件通过 UTF-8 无 BOM 检查。最终从项目根目录实际启动、查看座位图并退出，确认六个数据文件和 40 条正常电脑数据，重启前后字节保持一致；输出保存在最终报告目录的 `root-smoke-output.txt`。

## 覆盖范围

| 类别 | 实际检查 |
|---|---|
| 首次启动 | 创建 data 和六文件；电脑恰为 1～40，状态全部正常 |
| 座位图 | 恰有 5 行、每行 8 座、每行 47 ASCII 列，编号与符号全部匹配 |
| 重启 | 故障电脑恢复为 `!`；乱序 CRLF 文件读取后按编号显示；六文件字节和电脑修改时间保持 |
| 正常流程 | 0 退出、1/2 后续开发提示、首尾空白的合法数字 |
| 非法菜单 | 字母、负数、范围外整数、溢出整数、空行、空白、10000 字节行、尾随文字、小数、NUL、正号、两个数字 |
| EOF | 立即结束、末行不带换行、非法/超长末行后结束、查看座位后结束 |
| 缺失数据 | 备份原目录后重建；零字节电脑文件初始化；已有电脑时补齐其他文件 |
| 损坏电脑文件 | 乱码、空白行、缺记录、重复/越界/负编号、非法状态、多记录/字段、尾随文字、溢出、NUL、超长行、BOM、非法字节、尾部空行 |
| 类型错误 | data 是普通文件；六个数据文件分别被目录占用 |
| 保存接口 | 直接保存测试故障状态，重读与跨进程座位显示均一致 |
| 保存校验 | 零编号、重复编号、非法状态、NULL 快照均被拒绝且原文件不变 |
| 创建失败 | 已有 .tmp 不覆盖；临时路径是目录时不破坏旧目标 |
| 替换失败 | Windows 实际锁住目标的删除共享权限，保存报错，旧目标不变、完整临时快照保留 |

## 已发现并修复

首次实际启动发现该 MinGW 工具链的 MSVCRT 不支持 `fopen("wbx")`，返回 `EINVAL`。已用 `_open(... _O_EXCL ...)` + `_fdopen` 在 Windows 下实现等价独占创建，其他平台仍使用 C11 接口。上述 57 项为修复后实测结果。

构建脚本首次在 Windows PowerShell 5.1 下执行时发现 UTF-8 无 BOM 中文字符串的解码问题。脚本的提示和注释已改用 ASCII（仍是合法 UTF-8），避免要求修改 PowerShell 的全局编码设置。

## 尚未验证

Linux/macOS 分支、不同 Windows C 运行库、磁盘实际耗尽/掉电和多进程并发写入未验证。P0 不承诺断电耐久或并发事务。自动测试检查 UTF-8 输出字节与 ASCII 座位布局；具体终端字体的视觉效果仍取决于本机终端设置。
