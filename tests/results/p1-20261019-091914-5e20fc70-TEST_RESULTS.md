# P1 自动测试记录

总计 134；通过 134；失败 0。

只记录测试名称与结果。密码输入不写日志，账户数据仅存在隔离用例的 data 文件。

| 用例 | 结果 |
|---|---|
| admin: 注册/重启登录/角色菜单/退出/未开放功能 | 通过 |
| admin: 多账户/重复拒绝/旧账户保留 | 通过 |
| admin: 错误密码/不存在/重试成功 | 通过 |
| admin: 密码确认不一致 | 通过 |
| admin: API roundtrip | 通过 |
| admin: API invalid-struct | 通过 |
| admin: API read-missing | 通过 |
| admin: API retry-write | 通过 |
| admin: API save-invalid | 通过 |
| admin: API read-locked | 通过 |
| admin: 输入拒绝 id-empty | 通过 |
| admin: 输入拒绝 id-long | 通过 |
| admin: 输入拒绝 id-separator | 通过 |
| admin: 输入拒绝 id-space | 通过 |
| admin: 输入拒绝 id-unicode | 通过 |
| admin: 输入拒绝 id-leading-dash | 通过 |
| admin: 输入拒绝 name-long | 通过 |
| admin: 输入拒绝 name-utf8-long | 通过 |
| admin: 输入拒绝 name-empty | 通过 |
| admin: 输入拒绝 name-separator | 通过 |
| admin: 输入拒绝 name-bad-utf8 | 通过 |
| admin: 输入拒绝 name-partial-utf8 | 通过 |
| admin: 输入拒绝 name-nul | 通过 |
| admin: 输入拒绝 password-long | 通过 |
| admin: 输入拒绝 password-empty | 通过 |
| admin: 输入拒绝 password-delimiter | 通过 |
| admin: 输入拒绝 password-huge | 通过 |
| admin: 输入拒绝 confirm-long | 通过 |
| admin: 最大字节容量/中文/CRLF | 通过 |
| admin: EOF 字段 0 | 通过 |
| admin: EOF 字段 1 | 通过 |
| admin: EOF 字段 2 | 通过 |
| admin: EOF 字段 3 | 通过 |
| admin: EOF 字段 4 | 通过 |
| admin: EOF 字段 5 | 通过 |
| admin: EOF 字段 6 | 通过 |
| admin: 登录后EOF | 通过 |
| admin: 多级菜单非法输入 | 通过 |
| admin: 损坏安全报错 empty-line | 通过 |
| admin: 损坏安全报错 missing-field | 通过 |
| admin: 损坏安全报错 extra-field | 通过 |
| admin: 损坏安全报错 duplicate | 通过 |
| admin: 损坏安全报错 bad-id | 通过 |
| admin: 损坏安全报错 long-id | 通过 |
| admin: 损坏安全报错 long-name | 通过 |
| admin: 损坏安全报错 long-password | 通过 |
| admin: 损坏安全报错 empty-name | 通过 |
| admin: 损坏安全报错 empty-password | 通过 |
| admin: 损坏安全报错 nul | 通过 |
| admin: 损坏安全报错 utf8 | 通过 |
| admin: 损坏安全报错 overlong-utf8 | 通过 |
| admin: 损坏安全报错 surrogate | 通过 |
| admin: 损坏安全报错 too-high-codepoint | 通过 |
| admin: 损坏安全报错 control | 通过 |
| admin: 损坏安全报错 bom | 通过 |
| admin: 损坏安全报错 huge-line | 通过 |
| admin: 损坏安全报错 trailing-blank | 通过 |
| admin: 容量边界 0 | 通过 |
| admin: 容量边界 1 | 通过 |
| admin: 原格式/末行无换行/旧时长保留 | 通过 |
| admin: 完整UTF8多字节字段往返 | 通过 |
| admin: 写入失败不虚报 stale | 通过 |
| admin: 写入失败不虚报 directory | 通过 |
| admin: Windows替换失败保留旧账户 | 通过 |
| student: 注册/重启登录/角色菜单/退出/未开放功能 | 通过 |
| student: 多账户/重复拒绝/旧账户保留 | 通过 |
| student: 错误密码/不存在/重试成功 | 通过 |
| student: 密码确认不一致 | 通过 |
| student: API roundtrip | 通过 |
| student: API invalid-struct | 通过 |
| student: API read-missing | 通过 |
| student: API retry-write | 通过 |
| student: API save-invalid | 通过 |
| student: API read-locked | 通过 |
| student: 输入拒绝 id-empty | 通过 |
| student: 输入拒绝 id-long | 通过 |
| student: 输入拒绝 id-separator | 通过 |
| student: 输入拒绝 id-space | 通过 |
| student: 输入拒绝 id-unicode | 通过 |
| student: 输入拒绝 id-leading-dash | 通过 |
| student: 输入拒绝 name-long | 通过 |
| student: 输入拒绝 name-utf8-long | 通过 |
| student: 输入拒绝 name-empty | 通过 |
| student: 输入拒绝 name-separator | 通过 |
| student: 输入拒绝 name-bad-utf8 | 通过 |
| student: 输入拒绝 name-partial-utf8 | 通过 |
| student: 输入拒绝 name-nul | 通过 |
| student: 输入拒绝 password-long | 通过 |
| student: 输入拒绝 password-empty | 通过 |
| student: 输入拒绝 password-delimiter | 通过 |
| student: 输入拒绝 password-huge | 通过 |
| student: 输入拒绝 confirm-long | 通过 |
| student: 最大字节容量/中文/CRLF | 通过 |
| student: EOF 字段 0 | 通过 |
| student: EOF 字段 1 | 通过 |
| student: EOF 字段 2 | 通过 |
| student: EOF 字段 3 | 通过 |
| student: EOF 字段 4 | 通过 |
| student: EOF 字段 5 | 通过 |
| student: EOF 字段 6 | 通过 |
| student: 登录后EOF | 通过 |
| student: 多级菜单非法输入 | 通过 |
| student: 损坏安全报错 empty-line | 通过 |
| student: 损坏安全报错 missing-field | 通过 |
| student: 损坏安全报错 extra-field | 通过 |
| student: 损坏安全报错 duplicate | 通过 |
| student: 损坏安全报错 bad-id | 通过 |
| student: 损坏安全报错 long-id | 通过 |
| student: 损坏安全报错 long-name | 通过 |
| student: 损坏安全报错 long-password | 通过 |
| student: 损坏安全报错 empty-name | 通过 |
| student: 损坏安全报错 empty-password | 通过 |
| student: 损坏安全报错 nul | 通过 |
| student: 损坏安全报错 utf8 | 通过 |
| student: 损坏安全报错 overlong-utf8 | 通过 |
| student: 损坏安全报错 surrogate | 通过 |
| student: 损坏安全报错 too-high-codepoint | 通过 |
| student: 损坏安全报错 control | 通过 |
| student: 损坏安全报错 bom | 通过 |
| student: 损坏安全报错 huge-line | 通过 |
| student: 损坏安全报错 trailing-blank | 通过 |
| student: 损坏安全报错 negative-time | 通过 |
| student: 损坏安全报错 overflow-time | 通过 |
| student: 损坏安全报错 text-time | 通过 |
| student: 容量边界 0 | 通过 |
| student: 容量边界 1 | 通过 |
| student: 原格式/末行无换行/旧时长保留 | 通过 |
| student: 完整UTF8多字节字段往返 | 通过 |
| student: 写入失败不虚报 stale | 通过 |
| student: 写入失败不虚报 directory | 通过 |
| student: Windows替换失败保留旧账户 | 通过 |
| 双角色隔离/同名不同凭据/退出后跨角色须重新认证 | 通过 |
| 登录只读取对应角色文件 | 通过 |
| 真实开发数据保持不变 | 通过 |
