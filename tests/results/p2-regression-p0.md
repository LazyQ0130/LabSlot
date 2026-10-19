# P0 自动测试记录

平台：win32
总计 57；通过 57；失败 0。

此处仅归档脱敏报告；原始测试数据与进程输出保留于本机 tests/runs/。

| 测试 | 结果 | 失败详情 |
|---|---|---|
| 首次启动/六文件/40台/5×8布局/正常退出 | 通过 |  |
| 重启/故障恢复/乱序CRLF/已有六文件不覆盖 | 通过 |  |
| 菜单非法输入：letter | 通过 |  |
| 菜单非法输入：negative | 通过 |  |
| 菜单非法输入：out-of-range | 通过 |  |
| 菜单非法输入：large-int | 通过 |  |
| 菜单非法输入：empty | 通过 |  |
| 菜单非法输入：spaces | 通过 |  |
| 菜单非法输入：long-line | 通过 |  |
| 菜单非法输入：trailing-text | 通过 |  |
| 菜单非法输入：decimal | 通过 |  |
| 菜单非法输入：nul | 通过 |  |
| 菜单非法输入：sign | 通过 |  |
| 菜单非法输入：two-numbers | 通过 |  |
| EOF：immediate | 通过 |  |
| EOF：no-newline | 通过 |  |
| EOF：after-invalid | 通过 |  |
| EOF：after-long | 通过 |  |
| EOF：after-seat | 通过 |  |
| 管理员/学生实际入口及合法空白 | 通过 |  |
| 缺失数据目录后恢复（旧目录保留备份） | 通过 |  |
| 零字节电脑文件初始化 | 通过 |  |
| 损坏保留原文件：garbage | 通过 |  |
| 损坏保留原文件：blank-only | 通过 |  |
| 损坏保留原文件：spaces-only | 通过 |  |
| 损坏保留原文件：missing-record | 通过 |  |
| 损坏保留原文件：duplicate | 通过 |  |
| 损坏保留原文件：bad-number | 通过 |  |
| 损坏保留原文件：zero-number | 通过 |  |
| 损坏保留原文件：negative-number | 通过 |  |
| 损坏保留原文件：bad-status | 通过 |  |
| 损坏保留原文件：extra-record | 通过 |  |
| 损坏保留原文件：extra-field | 通过 |  |
| 损坏保留原文件：suffix | 通过 |  |
| 损坏保留原文件：overflow | 通过 |  |
| 损坏保留原文件：nul | 通过 |  |
| 损坏保留原文件：long-line | 通过 |  |
| 损坏保留原文件：utf8-bom | 通过 |  |
| 损坏保留原文件：invalid-utf8 | 通过 |  |
| 损坏保留原文件：trailing-blank | 通过 |  |
| 电脑文件末行无换行 | 通过 |  |
| 补齐缺失业务文件并保留电脑 | 通过 |  |
| 数据目录被同名文件占用 | 通过 |  |
| 数据文件类型错误：admins.txt | 通过 |  |
| 数据文件类型错误：students.txt | 通过 |  |
| 数据文件类型错误：computers.txt | 通过 |  |
| 数据文件类型错误：reservations.txt | 通过 |  |
| 数据文件类型错误：error.txt | 通过 |  |
| 数据文件类型错误：count.txt | 通过 |  |
| 保存接口/临时替换/故障状态跨进程持久化 | 通过 |  |
| 保存拒绝无效快照：invalid-no | 通过 |  |
| 保存拒绝无效快照：duplicate | 通过 |  |
| 保存拒绝无效快照：invalid-status | 通过 |  |
| 保存拒绝无效快照：null | 通过 |  |
| 临时文件独占创建/遗留文件不覆盖 | 通过 |  |
| 临时文件创建失败不破坏旧数据 | 通过 |  |
| Windows替换失败保留原文件和临时快照 | 通过 |  |
