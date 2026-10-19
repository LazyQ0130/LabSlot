# 脱敏验收记录

此目录保存文档引用的 P0～P5.1 实测报告及构建记录。仅归档脱敏文本、规范换行和行尾空白，并将本机绝对目录替换为项目相对路径。

原始测试目录、账户测试数据、临时文件、可执行文件和逐用例进程日志仍保留在本机 `tests/runs/`，不提交到 GitHub。报告不包含密码或账户原文。

运行 `build.ps1 -Test` 或 `mingw32-make test` 可在独立目录复现测试。

P5.1 基线为 577/577；最终完整回归 577/577，新增 GUI 检查 93 项通过（含 24 个 DPI/尺寸布局场景及 5 个夹具原字节保护）。详见 `p5-baseline.log`、`p5-final-verification.log`、`p5-analysis.log` 和 `p5-make-build.log`。`build.ps1 -Test -GuiTest` 复现双目标构建与全部测试；人工/系统 DPI 验证边界见 [GUI 验收记录](../P5_GUI_TEST_CASES.md)。
