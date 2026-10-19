# 脱敏验收记录

此目录保存文档引用的 P0～P5.3 实测报告及构建记录。仅归档脱敏文本、规范换行和行尾空白，并将本机绝对目录替换为项目相对路径。

原始测试目录、账户测试数据、临时文件、可执行文件和逐用例进程日志仍保留在本机 `tests/runs/`，不提交到 GitHub。报告不包含密码或账户原文。

运行 `build.ps1 -Test` 或 `mingw32-make test` 可在独立目录复现测试。

P5.1 基线为 577/577；最终完整回归 577/577，新增 GUI 检查 93 项通过（含 24 个 DPI/尺寸布局场景及 5 个夹具原字节保护）。详见 `p5-baseline.log`、`p5-final-verification.log`、`p5-analysis.log` 和 `p5-make-build.log`。`build.ps1 -Test -GuiTest` 复现双目标构建与全部测试；人工/系统 DPI 验证边界见 [GUI 验收记录](../P5_GUI_TEST_CASES.md)。

P5.2 开发前基线 577+93 全部通过；最终仍为 P0–P4 577/577、P5.1 GUI 93/93，新增学生 GUI 263/263。见 `p5-2-baseline.log`、`p5-2-final-verification.log`、`p5-2-student-final.md`、`p5-2-analysis.log`、`p5-2-make-build.log`。新增统计包括 253 项 C 驱动检查与 10 项 Python 文件保护检查；实际系统 DPI/多显示器人工验收边界见 [P5.2 记录](../P5_2_GUI_TEST_CASES.md)。

P5.3 开发前 933 项基线全通过；最终原有 577+93+263 项保持通过，新增管理员 GUI 359/359，累计 1292/1292。新增包括 346 项 C 驱动检查与 13 项 Python 文件保护，含 42 个管理员 DPI/尺寸场景、真实归档锁失败后的下一进程启动恢复、100000 预约/10000 报告及 80 次资源循环。见 `p5-3-baseline.log`、`p5-3-final-verification.log`、`p5-3-admin-final.md`、`p5-3-make-build.log` 和 [P5.3 记录](../P5_3_GUI_TEST_CASES.md)。实际系统缩放、多显示器和输入法人工验证边界另行说明，截图仅在本地保留。
