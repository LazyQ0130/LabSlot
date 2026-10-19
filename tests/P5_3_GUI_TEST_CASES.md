# P5.3 管理员 Win32 GUI 验收

## 范围与基线

基于 `0aea48c` 实现管理员工作台、电脑维护、报告处理、使用情况、精确学生查询、日报/周报及每日统计导出。保留学生六项导航、原有数据格式、P3 恢复机制和两个可执行目标。纯 C11、Win32、Common Controls 与 GDI，无新增运行依赖；没有开发新业务或正式课程设计报告。

开发前阅读 GUI 页面的实际创建、布局、通知和销毁代码，及 admin/fault/stats/storage/auth/booking 的接口与实际实现，核对构建、README 和既有验收文档。页面旧值 0–6、控件 100–115、200–239 和 300–318 保持稳定。管理员新增页面 7–11，控件 400–427，维护座位 500–539。

修改前执行 `build.ps1 -Test -GuiTest`，严格双目标构建通过，577+93+263=**933 项全部通过**。见 [基线记录](results/p5-3-baseline.log)。

## 页面与真实业务接口

管理员导航为工作台、实验室座位、电脑维护、故障报告、使用情况、学生查询、数据统计、退出登录，均对应真实功能。学生导航和业务保持原状。浅灰背景、白卡、蓝色主色、中文字体、危险操作红色、按钮悬停/焦点/禁用状态沿用原主题。

| 页面/操作 | 使用的现有 API | 交互与口径 |
|---|---|---|
| 管理工作台 | `fault_read_computers`、`fault_query_reports`、`admin_query_usage` | 真实总数/正常/故障、当前时段预约人数、待处理数；报告集合取数量后即释放；非开放时段独立标注，读取失败显示不可用 |
| 实验室座位 | 原 `booking_get_seats` 页面 | 保留日期与九时段，只读状态图；管理员无法预约 |
| 电脑维护 | `fault_require_role`、`fault_preview_change`、`fault_change_computer` | 5×8 可选电脑、当前状态和当前时段预约；预览未来预约、默认否确认、提交后刷新 |
| 管理员报告 | `fault_query_reports`、`fault_get_report`、`fault_preview_process`、`fault_process` | 六列虚拟表格，全/待处理/已处理及电脑筛选，最新详情与三种动作 |
| 使用情况 | `admin_query_usage` | 日期/九时段，左侧 5×8 图、右侧学号/姓名/电脑/预约编号；人数取 `AdminUsage.count`，不以故障优先座位符号的 `#` 数代替 |
| 学生查询 | `admin_query_student`、`admin_free_student` | 完整学号精确查询，有效/全部历史，基本信息与五列明细；失败释放旧学生结果，不返回密码或缓存时长 |
| 每日统计 | `stats_compute_daily` | 有效预约数、去重人数、小时、利用率；九根 GDI 柱直接读取 `DailyStats.counts.slot_counts`，纵轴 0–40 |
| 每周统计 | `stats_compute_week` | 完整周一至周日、整周小时/去重人数/日均小时/利用率；七根柱直接读取 `WeeklyStats.days[].counts.reserved_hours`，纵轴 0–360 |
| 每日导出 | `stats_export_daily` | 先展示所选日期与当前概要，默认否确认；业务重新计算/upsert，成功说明快照可能过时；周模式不允许导出 |

所有人数和小时表示预约事实，不代表签到或实际上机。每日容量 360、整周容量 2520；整周去重人数来自 `WeeklyStats.student_count`，不累加每日人数。GUI 没有重新读取 TXT 聚合、伪造学生会话或读取 `Student.time`。

## 维护、确认与失败处理

维护和报告处理共享独立的原生确认窗口：摘要说明电脑状态变化、是否关闭报告及取消未来预约数量；下方为可滚动的虚拟表格，显示预约编号、日期、时段、学号、电脑。集合由调用者独占，弹窗只借用；默认焦点为否，Escape/关闭放弃，Enter 只有焦点明确位于是按钮时才确认。没有把长记录拼接进 MessageBox。

确认后分别调用 `fault_change_computer` / `fault_process` 重新验证账户、时间、记录状态和恢复门禁。只取消严格未开始的有效预约；已开始和历史保留，恢复正常不恢复取消记录。重复操作和确认期间的竞争修改展示实际业务错误，不提前修改页面状态或文件。

`FAULT_RECOVERY_REQUIRED` 明确提示“维护未完成、未确认成功”，禁用相关修改，要求先完整备份 data 再按现有流程重启。GUI 不清除日志；正式启动仍先 `storage_recover_maintenance` 再初始化。提交成功后的查询/电脑刷新失败明确显示“提交成功，但刷新失败”，区别于提交未完成。

新增恢复集成用例在独立测试目录用真实 Windows 排他文件句柄锁住完成归档文件，使维护更新后无法归档而进入恢复门禁；关闭窗口保留意图，下一独立进程调用正常 GUI 初始化完成重放并校验未来预约取消、已开始预约保留。此用例验证归档失败后的恢复；原 P3/P4 回归继续覆盖 `_Exit` 中断、分步保存及重放，不将文件锁用例冒充物理断电测试。

## 模块与生命周期

| 新增文件 | 职责 |
|---|---|
| `include/gui_admin.h` | 管理员页面、公共组件和消息接口 |
| `src/gui/gui_admin_common.c` | 实际账户权限门禁、错误/恢复提示、共享日期/时段、只读座位图、可滚动确认窗口 |
| `src/gui/gui_admin_dashboard.c` | 工作台真实投影与快捷操作 |
| `src/gui/gui_admin_lab.c` | 可选电脑图、维护预览与提交 |
| `src/gui/gui_admin_reports.c` | 报告虚拟表格、筛选、详情及三种处理动作 |
| `src/gui/gui_admin_usage.c` | 指定日期/时段预约人数、座位及学生列表 |
| `src/gui/gui_admin_students.c` | 精确查询、历史筛选及自有学生集合 |
| `src/gui/gui_admin_stats.c` | 日/周统计、原生柱状图、确认导出 |
| `tests/gui_admin_probe.c`、`tests/test_gui_admin.py` | 真实 Windows 消息/控件集成、隔离夹具、时钟/一次刷新失败注入与文件保护 |
| 本文及 `results/p5-3-*` | 脱敏验收文本 |

修改 `gui_app.h/c` 的投影、导航和统一分发；`gui_dashboard.c` 负责角色导航并将管理员工作台委托专门模块；`gui_theme.c` 分派绘制；`gui_widgets.c` 统一清理学生查询集合并复用按钮状态。`build.ps1`、Makefile 和 VS Code 完整验证任务纳入第三套 GUI 测试，GUI 源文件通配列表自动纳入新增模块。README 和 results/README 同步范围及复现方法。

P5.1 驱动仅将管理员座位页的精确控件数断言 46→51，对应五个真实导航；仍检查不可预约。其他断言未弱化，P5.2 驱动及 P0–P4 测试未修改。正式 auth/booking/fault/admin/stats/storage/lab/utils、控制台入口和数据格式均未修改。

长预约、报告和受影响预约均使用 `LVS_OWNERDATA`，通知只读页面持有的集合；滚动和 `WM_PAINT` 不查询文件。`admin_student.records` 用 `admin_free_student`，报告用 `storage_free_reports`，预览用 `storage_free_reservations`；不浅复制后分别释放。页面切换、注销、关闭和失败均清理自有结果；确认窗口独立拥有字体/控件，不共享需销毁的主题句柄。DPI 改变重建字体、布局和表头。

## 严格构建与回归

复现命令：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\build.ps1 -Test -GuiTest
# 单独管理员集成
python tests/test_gui_admin.py --cc gcc
# 或 make test-gui-admin
```

Windows / GCC 16.2.0，两个目标均使用 `-std=c11 -Wall -Wextra -Wpedantic -Werror`，GUI 链接 `-mwindows -lcomctl32 -lgdi32 -luser32` 及 Common Controls/PerMonitorV2 清单。最终结果见 [完整构建回归](results/p5-3-final-verification.log) 和 [管理员集成明细](results/p5-3-admin-final.md)。

另用 `make -B all` 强制重建控制台、资源清单和 GUI，退出码为 0，新增七个 GUI 源文件均已纳入严格构建。见 [Make 构建记录](results/p5-3-make-build.log)。

2026-10-19 最终完整命令退出码为 0，结果如下：

| 检查 | 结果 |
|---|---:|
| P0 | 57/57 |
| P1 | 134/134 |
| P2 | 155/155 |
| P3 | 119/119 |
| P4 | 112/112 |
| P0–P4 合计 | **577/577** |
| P5.1 GUI | **93/93** |
| P5.2 GUI | **263/263** |
| P5.3 GUI | **359/359** |
| 累计 | **1292/1292** |

新增 359 项由 346 项 C 驱动检查与 13 项 Python 文件保护检查组成。normal 180、student 11、empty 9、corrupt 9、corrupt-reports 7、save-failure 14、changed 19、refresh-failure 36、recovery 19、recover-restart 9、large 9、preview-many 11、account-gone 13 项 C 检查全部通过。

## 新增管理员集成覆盖

所有场景创建真实 Win32 主窗口并通过 `WM_COMMAND`、`WM_NOTIFY`、日期/组合框、ListView 选择/虚拟行、实际确认窗口和 `WM_DPICHANGED` 操作生产 GUI 模块，不仅调用业务 API。业务 API 在测试中用于验证持久结果和精确构造竞争条件。

| 场景 | 验证内容 |
|---|---|
| normal | 实际登录、八项导航、真实设备/报告/当前时段人数，非开放时段，维护预览/放弃/确认/重复/恢复，未来取消及已开始保留，三种报告动作与筛选/详情 |
| normal 查询/统计/导出 | 精确学号及失败清空、本人历史、公开字段不含密码/缓存；日/周精确计数/去重/利用率/柱数据，放弃导出、成功快照、业务变化后同日更新 |
| student | 六项学生导航保持，伪造五个管理员导航及管理员业务入口拒绝 |
| empty | 无预约/报告的合法零值、空表及零统计区别于失败 |
| corrupt / corrupt-reports | 损坏数据明确错误，清空旧明细、统计不可用、导出禁用 |
| save-failure | 已占用 count 临时路径，真实保存失败不显示导出成功 |
| changed | 确认期间另一次维护/报告处理已提交，GUI 最终提交返回真实重复/状态错误 |
| refresh-failure | 实际报告提交成功后，一次报告重查返回错误，显示提交成功与刷新失败的区别 |
| recovery + recover-restart | 真实归档文件锁导致维护未完成、禁用相关查询/修改；下一进程正常启动重放 |
| large | 100000 条历史、10000 条报告使用虚拟表，末行可读 |
| preview-many | 41 条未来预约的原生确认表格可滚动到末行，放弃后文件不变 |
| account-gone | 实际管理员账户失效后，旧会话不能查询统计、导出或维护 |

固定时钟和一次刷新读取失败仅由独立测试链接重命名 utils/fault 符号注入；生产 EXE 没有测试参数、固定日期、自动确认或测试分支。测试线程等确认窗口可见并完成初始焦点后再断言默认否，避免把 ShowWindow 中途状态当成最终焦点。失败注入不改写业务成功输出。

每个场景保护未授权修改文件的 SHA-256 和修改时间，检查允许的业务变化；最后对真实开发 data 作相同保护。账号夹具和原始进程日志保留在忽略的 tests/runs 中，Git 仅归档脱敏结果。开发过程曾发现并修复工作台无需持有报告明细、弹窗焦点观察过早和刷新失败注入点错误，最终结果不掩盖开发失败记录。

## DPI、资源及实际画面

管理员七页在 96/120/144 DPI、1000×650 和 1280×800 逻辑客户区下，共 42 个真实 `WM_DPICHANGED` 场景，检查控件在客户区内、互不重叠、表格列宽不越界。原 P5.1/P5.2 的 24+30 个场景继续保留。预热后 80 次“学生查询→报告→维护→统计”循环，检查动态集合释放及 GDI/USER 计数，记录每 20 次的中间值。

本次资源基线 GDI **35**、USER **29**；第 20/40/60/80 次均为 GDI **35**、USER **29**，无持续增长。关闭后字体、画刷、控件、集合和会话清空。

使用 Windows 自动化技能检查真实窗口，保存并逐张查看以下本地截图，中文、状态、表格、确认默认否、图表刻度及真实查询值均可见。截图使用独立 tests/runs 目录内的确定性合成夹具，通过实际业务 API 查询和生产 GUI 模块渲染；不是生成图或生产 EXE 内置演示数据。图表截图日报 166 小时、整周 548 小时及整周去重 32 人均由该夹具的业务统计计算。按仓库规则，图片保留本地、不纳入 Git。

- [管理员工作台](../docs/screenshots/p5-3-dashboard.jpg)
- [电脑维护与所选电脑](../docs/screenshots/p5-3-lab.jpg)
- [故障处理确认与可滚动影响列表](../docs/screenshots/p5-3-report-confirm.jpg)
- [实验室预约查询](../docs/screenshots/p5-3-usage.jpg)
- [学生精确查询](../docs/screenshots/p5-3-students.jpg)
- [每日统计柱状图](../docs/screenshots/p5-3-daily.jpg)
- [每周统计柱状图](../docs/screenshots/p5-3-weekly.jpg)

实际操作核对了导航、选择电脑/报告、打开并放弃确认、输入学号后按 Enter 查询、日/周切换及窗口最大化；额外保存 [最大化周报](../docs/screenshots/p5-3-weekly-maximized.jpg)。当前环境的 1280×800 逻辑窗口画面已查看，1000×650 最小尺寸由上述消息布局测试验证。没有修改 Windows 系统缩放设置，实际系统 125%/150%、多显示器 DPI 拖动及完整中文输入法人工验收仍未验证。

## 已知限制与后续验收建议

沿用单进程教学 TXT 存储；GUI 单实例不协调控制台或外部编辑器。页面数据是进入/刷新的快照，没有后台订阅，提交仍由业务重新验证；预览期间新增/变化的预约以最终提交为准。单线程同步读取大文件可能短暂阻塞，未引入复杂线程或分页存储。现有恢复机制不承诺物理断电级跨文件原子性。

确认表与业务表均可滚动，描述摘要的完整内容通过详情查看。自绘标题/统计图的屏幕阅读器语义、图表悬停提示、完整键盘辅助体验尚可继续优化；本阶段优先保留原生控件和真实计算。下一次最终验收建议补测实际系统缩放、多显示器、中文输入法、最大化/最小窗口及真实设备，整理备份恢复操作指引；不预先扩展新业务或编写课程报告。
