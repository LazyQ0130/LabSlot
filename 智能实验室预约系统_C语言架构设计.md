# 智能实验室预约系统——C 语言课程设计架构说明书

> 版本：V1.0｜技术基线：C11、GCC/Clang/MSVC C 编译器、终端交互、文本文件持久化。
>
> 需求依据：用户提供的《智能实验室预约系统》三页题目。本文将“题目必做”和“建议扩展”显式区分。

## 1. 项目目标与边界

### 1.1 题目必做

1. 40 台电脑，08:00—17:00 开放，每个预约时段为 1 小时，共 9 段。
2. 管理员与学生两种角色，都须先注册再用密码登录。
3. 管理员：维护电脑故障状态（`!`）、查询当前实验室使用和学生信息、导出每日统计到 `count.txt`。
4. 学生：按时段查看座位、选座预约、取消预约、报故障到 `error.txt`。
5. 座位状态可视化：空闲 `*`、已预约 `#`、故障 `!`；多级菜单。
6. 按课程设计要求详细注释：变量、函数功能、参数、返回值、关键语句；命名规范、结构清楚。

### 1.2 题目选做

- 输入校验、管理员周度统计、学生累计实验时间。
- 彩色或图形化界面。

### 1.3 本方案为保证可用性额外制定的业务约定

题目没有详细定义预约日期、历史记录、故障与预约冲突等行为，建议采用如下**实现约定**（不是题目原文）：

- 支持指定预约日期（输入 `YYYY-MM-DD`），仅允许预约未开始的时段。
- 允许同一学生在不同时间预约，但同一日期同一时段仅能持有一个有效预约。
- 同一日期、同一时段、同一电脑，仅能被一位学生有效预约。
- 仅能取消本人尚未开始的预约；不物理删除，改为“已取消”。
- 学生提交故障报告不直接改变电脑状态；管理员确认后才标记故障。
- 故障电脑禁止新预约。管理员标故障前应查看受影响的未来预约，并进行失效处理与提示，防止故障电脑继续被使用。
- 没有签到功能时，“当前实验人数”实际上是“当前时段有效预约人数”，并不等于实际到场人数；“累计实验时间”也只能准确表述为“已过去时段的预约小时数”。如果老师要求真实使用量，应新增签到/签退功能。
- 统计的理论总容量为 `40 × 9 = 360 座位小时/日`；没有故障历史时，利用率按此固定理论容量计算，并在报表注明口径。

## 2. 架构与依赖

```text
main.c（启动、加载、主循环、退出）
  └─ ui.c（主菜单、角色菜单、表格与座位图、输入）
       ├─ auth.c（账号注册/登录/权限）
       ├─ booking.c（查询/预约/取消/预约冲突）
       │    └─ lab.c（电脑状态、座位图数据）
       ├─ fault.c（学生报障/管理员确认处理）
       └─ stats.c（日统计/周统计/累计时长）

上述业务模块共同依赖：
  model.h（结构体、枚举、常量）
  storage.c（加载、保存、导出文本文件）
  utils.c（输入与日期时间校验、时间段映射）
```

### 2.1 项目目录

```text
smart-lab/
├── README.md
├── Makefile                  # GCC / MinGW 编译；Windows 可用 build.bat 替代
├── .gitignore
├── include/
│   ├── model.h              # 所有模型与枚举、容量常量
│   ├── auth.h               # 账号函数接口
│   ├── lab.h                # 电脑管理与座位查询接口
│   ├── booking.h            # 预约接口
│   ├── fault.h              # 报障接口
│   ├── stats.h              # 报表接口
│   ├── storage.h            # 文件读写接口
│   ├── utils.h              # 日期、输入及校验工具接口
│   └── ui.h                 # 菜单与展示接口
├── src/
│   ├── main.c
│   ├── auth.c
│   ├── lab.c
│   ├── booking.c
│   ├── fault.c
│   ├── stats.c
│   ├── storage.c
│   ├── utils.c
│   └── ui.c
├── data/                    # 运行数据；首次运行时创建或初始化
│   ├── admins.txt
│   ├── students.txt
│   ├── computers.txt
│   ├── reservations.txt
│   ├── error.txt            # 题目明确要求
│   └── count.txt            # 题目明确要求
└── tests/
    └── TEST_CASES.md        # 手工测试记录；可追加自动测试
```

`error.txt`、`count.txt` 始终位于 `data/` 下。如老师要求文件必须生成在可执行文件当前目录，可以将文件路径宏改为 `"error.txt"` 和 `"count.txt"`。

## 3. 数据模型（model.h）

> 原题 `struct lab` 包含 `eight` 到 `sixteen` 九个时段状态，只能表达某一天的状态，且单个 `name`/`number` 无法表示同一电脑不同时段的不同学生。本方案保留 `Lab` 基本概念，将“日期+时段+学生+座位”关系放到独立 `Reservation` 中。若老师要求逐字保留原始 `struct lab` 字段，可以增加九项当天缓存用于展示，但**不要**把它们作为唯一的预约数据源。

```c
#ifndef MODEL_H
#define MODEL_H

#define COMPUTER_COUNT 40
#define SLOT_COUNT 9
#define OPEN_HOUR 8
#define CLOSE_HOUR 17

#define ID_LEN 20
#define NAME_LEN 48
#define PASSWORD_LEN 64
#define DETAIL_LEN 160

/* 静态内存容量：用于课程设计演示；达到上限时必须给出错误提示。 */
#define MAX_ADMINS 30
#define MAX_STUDENTS 1000
#define MAX_RESERVATIONS 100000
#define MAX_REPORTS 10000

/* 电脑状态：0 正常、1 故障。 */
typedef enum {
    COMPUTER_NORMAL = 0,
    COMPUTER_FAULT = 1
} ComputerStatus;

/* 预约状态：0 已取消、1 有效。过去的有效预约表示历史记录。 */
typedef enum {
    BOOKING_CANCELLED = 0,
    BOOKING_ACTIVE = 1
} BookingStatus;

/* 故障报告处理状态。 */
typedef enum {
    REPORT_PENDING = 0,
    REPORT_PROCESSED = 1
} ReportStatus;

/* 管理员账号。 */
typedef struct {
    char id[ID_LEN];           /* 工号：唯一标识 */
    char name[NAME_LEN];       /* 姓名 */
    char password[PASSWORD_LEN]; /* 教学演示凭据，正式系统不能明文保存 */
} Admin;

/* 学生账号。 */
typedef struct {
    char id[ID_LEN];           /* 学号：唯一标识 */
    char name[NAME_LEN];       /* 姓名 */
    char password[PASSWORD_LEN]; /* 教学演示凭据，正式系统不能明文保存 */
    int time;                  /* 缓存：过去有效预约时段的累计小时数 */
} Student;

/* 每台电脑固定基础属性；临时预约状态须由预约记录动态求得。 */
typedef struct {
    int no;                   /* 编号 1～40 */
    ComputerStatus status;    /* 正常或故障 */
} Lab;

/* 一条预约记录，按 日期+时段+电脑 唯一占用。 */
typedef struct {
    int id;                   /* 预约号，自增且不复用 */
    char student_id[ID_LEN];  /* 预约学生学号 */
    int computer_no;          /* 电脑编号 1～40 */
    int date;                 /* YYYYMMDD 整数形式，例如 20261020 */
    int slot_index;           /* 0～8，0代表08:00～09:00 */
    BookingStatus status;     /* 有效或取消 */
} Reservation;

/* 故障报告；故障报告与电脑最终故障状态分离。 */
typedef struct {
    int id;                   /* 报告编号 */
    char student_id[ID_LEN];  /* 报告学生 */
    int computer_no;          /* 报障电脑 */
    int date;                 /* 上报日期 YYYYMMDD */
    char detail[DETAIL_LEN];  /* 故障说明，输入需限制长度/分隔符 */
    ReportStatus status;      /* 待处理或已处理 */
} FaultReport;

/* 运行时全部数据：程序入口定义为静态存储对象，不要放在 main 的栈上。 */
typedef struct {
    Admin admins[MAX_ADMINS];
    int admin_count;
    Student students[MAX_STUDENTS];
    int student_count;
    Lab computers[COMPUTER_COUNT];
    Reservation reservations[MAX_RESERVATIONS];
    int reservation_count;
    FaultReport reports[MAX_REPORTS];
    int report_count;
    int next_reservation_id;
    int next_report_id;
} AppState;

#endif
```

上述 `AppState` 为简单可靠的固定容量方案，避免课程设计初版中的动态内存管理错误。之后若要长期运行，可将预约/报告数组改为 `malloc` / `realloc` 动态扩容。注意本模型没有实施密码哈希，**不能**直接用于真实生产环境。

## 4. 文件存储格式与约束

采用 UTF-8 文本文件，每行一条记录，用 `|` 分隔字段；姓名和故障说明禁止输入换行、`|` 等分隔符。读取时用 `fgets` 配合**宽度限制**的解析方式，逐行检查字段数、长度、数值范围和重复主键；无效行报错并可跳过，不能把未知数据直接当正常数据使用。

| 文件 | 每行字段 | 示例（仅示意） |
|---|---|---|
| `admins.txt` | 工号 \| 姓名 \| 密码 | `A001|管理员甲|demo123` |
| `students.txt` | 学号 \| 姓名 \| 密码 \| 累计时长 | `S0001|学生甲|demo123|2` |
| `computers.txt` | 编号 \| 状态 | `1|0` |
| `reservations.txt` | 预约号 \| 学号 \| 电脑号 \| 日期 \| 时段索引 \| 状态 | `1001|S0001|10|20261020|2|1` |
| `error.txt` | 报告号 \| 学号 \| 电脑号 \| 日期 \| 说明 \| 状态 | `501|S0001|10|20261020|屏幕黑屏|0` |
| `count.txt` | 日期 \| 座位小时 \| 独立学生数 \| 峰值同时段人数 \| 理论利用率(%) | `20261020|90|25|19|25.00` |

`error.txt` 是业务源数据；`count.txt` 是**由预约记录计算的导出报表**，不是统计真值来源。导出可以重新计算后覆写/更新对应日期，避免重复统计。

保存步骤：先校验业务规则 → 更新内存 → 把完整记录集写入 `.tmp` → 检查 `fprintf` / `fclose` 返回值 → 尝试替换目标文件；失败时提示并恢复内存或重新加载。C 标准 `rename()` 在不同操作系统上覆盖已存在目标文件的行为不同；如用 Windows，应实现备份和替换回滚逻辑，不能把“单文件重命名”描述为跨文件事务。

首次运行：若文件不存在，创建 40 条电脑数据（编号 1～40，初始正常），其余用户、预约、报障文件为空；**已有文件读取失败时不要静默初始化并覆盖原数据**。

## 5. 模块与核心接口

以下接口用于指导开发，函数实现可按实际情况调整；`const` 用于只读数据查询，修改状态时传入 `AppState *`。

### 5.1 auth.h / auth.c（账号模块）

```c
int register_admin(AppState *app, const char *id, const char *name,
                   const char *password);
int register_student(AppState *app, const char *id, const char *name,
                     const char *password);
int login_admin(const AppState *app, const char *id,
                const char *password);    /* 返回 admins[] 索引，失败 -1 */
int login_student(const AppState *app, const char *id,
                  const char *password);  /* 返回 students[] 索引，失败 -1 */
int find_student_index(const AppState *app, const char *id);
```

约束：同一角色内部账号 ID 不可重复；全部输入限制长度；登录需匹配当前角色，管理员功能仅在管理员会话中可达。题目要求管理员可注册，但真实系统不能让陌生用户任意创建管理员账户；课程设计可设管理员注册码或演示环境初始管理员，属于额外安全设计。

### 5.2 lab.h / lab.c（电脑状态及座位视图）

```c
void init_computers(AppState *app);   /* 仅在首次初始化时执行 */
int computer_is_usable(const AppState *app, int computer_no);
int set_computer_status(AppState *app, int computer_no,
                        ComputerStatus new_status);
char get_seat_symbol(const AppState *app, int date,
                     int slot_index, int computer_no);
void print_seat_map(const AppState *app, int date, int slot_index);
```

`get_seat_symbol` 优先级：故障 `!` > 当前日期和时段存在有效预约 `#` > 空闲 `*`。座位图 5 行 × 8 列，在每个符号旁打印两位编号，便于选择。故障切换时的受影响预约应由管理员业务流程联合处理，不能只改电脑状态而忽略未来预约。

### 5.3 booking.h / booking.c（预约核心）

```c
int is_seat_booked(const AppState *app, int date,
                   int slot_index, int computer_no);
int has_student_time_conflict(const AppState *app,
                              const char *student_id,
                              int date, int slot_index);
int create_booking(AppState *app, const char *student_id,
                   int date, int slot_index, int computer_no);
int cancel_booking(AppState *app, int reservation_id,
                   const char *student_id);
void print_student_bookings(const AppState *app,
                            const char *student_id);
```

`create_booking` 必须逐步校验：学生存在 → 日期合法 → 时段 0～8 → 开始时间在未来 → 电脑号 1～40 → 电脑正常 → 该电脑在对应时段无有效预约 → 学生同一时段无有效预约 → 容量足够 → 保存记录成功。任何一步失败都不得留下有效预约。

`cancel_booking` 必须验证预约号存在、归本人所有、仍有效、尚未开始。取消状态记为 `0` 而不删除，保证统计与异常追查可用。

### 5.4 fault.h / fault.c（故障管理）

```c
int submit_fault_report(AppState *app, const char *student_id,
                        int computer_no, int date, const char *detail);
void print_pending_reports(const AppState *app);
int process_fault_report(AppState *app, int report_id);
int mark_computer_faulty(AppState *app, int computer_no);
int repair_computer(AppState *app, int computer_no);
```

学生报障时生成 `error.txt` 报告，电脑不立即变更为故障；管理员处理时可以：确认故障并停用、判定误报、将已修复电脑恢复正常。`process_fault_report` 可根据管理员选择设置处理状态。确认故障应展示未来有效预约清单并将其取消/通知，再使该电脑不可预约；文件保存要注意跨文件一致性。

### 5.5 stats.h / stats.c（统计）

```c
typedef struct {
    int date;               /* YYYYMMDD */
    int booked_seat_hours;  /* 当日有效预约条数，每条 1 小时 */
    int unique_students;    /* 当天去重学生数 */
    int peak_users;         /* 9个时段中的最大有效预约人数 */
    double utilization;     /* booked_seat_hours / 360.0 * 100 */
} DailyStats;

DailyStats calculate_daily_stats(const AppState *app, int date);
int export_daily_stats(const AppState *app, int date);
void print_weekly_stats(const AppState *app, int monday_date);
int calculate_student_elapsed_hours(const AppState *app,
                                    const char *student_id);
void print_current_usage(const AppState *app);
```

日报从预约源数据**实时重算**；周报按连续七天逐日计算，不要直接把 `YYYYMMDD` 整数加 1，因为跨月/跨年会出错。建议实现 `date_add_days()` 日期工具。累计时间只有“过去且未取消的预约小时数”，并非真实出勤小时数；如有签到模块再改成实际时间。

### 5.6 storage.h / storage.c（持久化）

```c
int ensure_data_directory(void);
int load_all(AppState *app);
int save_admins(const AppState *app);
int save_students(const AppState *app);
int save_computers(const AppState *app);
int save_reservations(const AppState *app);
int save_reports(const AppState *app);
```

`load_all` 先初始化计数器，再逐文件加载、校验上限、验证电脑编号和日期等；恢复 `next_reservation_id = max(existing_id) + 1`，`next_report_id` 同理。将 `count.txt` 看作导出物，启动时不必从它恢复统计状态。失败不可静默覆盖。

### 5.7 utils.h / utils.c（通用工具）

```c
int read_int_range(const char *prompt, int min_value, int max_value,
                   int *out_value);
int read_line(const char *prompt, char *buffer, int buffer_size);
int parse_date(const char *date_str, int *out_yyyymmdd);
int is_valid_date(int yyyymmdd);
int is_slot_future(int yyyymmdd, int slot_index);
int date_add_days(int yyyymmdd, int day_offset);
void format_slot(int slot_index, char *buffer, int buffer_size);
```

建议一律 `fgets` + `strtol` 处理菜单输入，检查完整消费、整数溢出和 EOF，避免 `scanf` 换行残留问题。日期须检查大小月及闰年；时间段映射 `start_hour = OPEN_HOUR + slot_index`。

### 5.8 ui.h / ui.c（界面）

```c
void show_main_menu(AppState *app);
void show_register_menu(AppState *app);
void show_student_menu(AppState *app, int student_index);
void show_admin_menu(AppState *app, int admin_index);
```

UI 只获取输入、调用业务函数、显示结果；**不得**在菜单中直接写文件或者修改预约数组。登录成功后保存对应角色的索引，仅把该角色可执行的操作显示在菜单中；退出登录返回初始主菜单。

## 6. 交互菜单（建议成品）

```text
======================================
        智能实验室预约管理系统
======================================
1. 学生注册
2. 学生登录
3. 管理员注册
4. 管理员登录
0. 退出系统
请选择：

------ 学生功能菜单 ------
1. 查看指定日期和时段的座位图
2. 预约电脑
3. 查询我的预约
4. 取消预约
5. 报告电脑故障
6. 查询累计已过去预约小时数 [选做]
0. 退出登录

------ 管理员功能菜单 ------
1. 查看电脑状态 / 设置故障或修复
2. 查看当前时段的预约人数和学生名单
3. 按学号查询学生
4. 查看并处理故障报告
5. 生成并导出当日使用报表
6. 统计一周使用情况 [选做]
0. 退出登录
```

座位图建议给出座位编号，区分状态、操作提示。例如 `01*` 表示 1 号空闲、`10#` 表示 10 号已预约、`25!` 表示 25 号故障。先选择日期和时段，再绘制 5×8 座位图；同一台电脑不同时段状态可不同。

## 7. 关键业务流程与状态一致性

### 7.1 预约

```text
学生登录 → 输入日期 → 选择时段 → 显示对应座位图
        → 输入电脑编号 → 检查日期/时间/电脑/冲突
        → 创建唯一预约编号 → 写入 reservations.txt
        → 成功后显示预约单；失败回滚并给原因
```

### 7.2 取消

```text
学生登录 → 查看本人预约 → 输入预约号
        → 核对本人 + 有效 + 尚未开始
        → 标记取消 → 保存 → 原时段座位重新空闲
```

### 7.3 故障

```text
学生提交报告 → 保存 error.txt → 管理员查看报告
       → 确认电脑故障（必要时取消未来受影响预约）
       → computers.txt 更新状态为故障
       → 维修完毕后恢复正常 → 允许新预约
```

### 7.4 统计

```text
管理员输入日期 → 读取内存有效预约记录 → 分时段统计
       → 计算去重学生数、峰值与理论利用率
       → 显示报表 → 更新/导出 count.txt
```

### 7.5 一致性检查

- 启动后，对所有**有效**预约核验学生存在、电脑号合法、日期与时段合法。
- 同一 `date + slot_index + computer_no` 不得有两个有效预约。
- 同一 `date + slot_index + student_id` 不得有两个有效预约。
- `Lab` 状态用于判断当前电脑是否可预约；故障电脑不能接收新预约。
- 任何影响业务记录的操作，在文件持久化失败时必须报告失败，不得假装成功。
- 数据文件的手工修改可能导致错误；在读取阶段应有冲突检测与报错。

## 8. 推荐开发顺序与验收门槛

| 阶段 | 范围 | 通过条件 |
|---|---|---|
| P0 项目基础 | 项目结构、编译脚本、结构体、读取工具、初始化电脑 | 可无警告编译，初次启动生成 40 台正常电脑 |
| P1 身份系统 | 两角色注册登录、重复 ID 校验、角色菜单 | 重复注册失败，错密码不能登录，退出登录返回主界面 |
| P2 预约核心 | 日期和时段、5×8 座位图、预约和取消 | 防同座位重订、防同一学生同时段重订；取消后座位恢复 |
| P3 故障与管理 | 故障报告、处理、故障/修复状态、学生查询、当前预约查询 | `error.txt` 正确；`!` 显示正确；故障后不能新预约 |
| P4 统计与扩展 | 日报 `count.txt`、周报、学生累计小时 | 各时段统计、去重人数、利用率准确；跨月周报准确 |
| P5 交付质量 | 输入校验、异常恢复、详细注释、README、测试记录 | 输入异常不崩溃，重启数据不丢，注释符合老师要求 |

推荐以 `-std=c11 -Wall -Wextra -Wpedantic` 编译；有条件使用 `-fsanitize=address,undefined` 测试。Windows 上 C11 兼容性取决于所用 MSVC 版本，也可使用 MinGW-w64 GCC。

## 9. 必须覆盖的测试用例

1. 首次运行 40 台电脑都显示空闲；以后启动不清空文件。
2. 学生/管理员正常注册、正常登录、密码错误、重复学号或工号。
3. 不合法日期（如 2026-02-29）、时段、电脑编号、超长输入或 EOF。
4. 相同日期/时段/座位重复预约必须失败。
5. 同一学生同日期/时段预约两个座位必须失败。
6. 同一台电脑在**不同时段**、**不同日期**可以分别被预约。
7. 学生 A 不能取消学生 B 的预约；已过去时段不能取消。
8. 取消后原电脑在对应日期/时段恢复 `*`，且历史记录保留。
9. 学生报故障后 `error.txt` 存在记录；管理员可查看并标记处理。
10. 管理员标故障后显示 `!` 且不能预约；修复后恢复 `*`（若没有有效预约）。
11. 电脑标故障前已有未来预约时，按约定处理并检查数据一致性。
12. 程序退出/重启后，用户、预约、故障、处理状态都能恢复。
13. 一天只预约 1 个小时：`booked_seat_hours = 1`，利用率 `1/360*100 ≈ 0.28%`。
14. 同一人一天预约多个小时，独立学生人数只计 1。
15. 一周统计经过月底、年底与闰年时日期推进正确。
16. 文件缺失、文件损坏、容量耗尽、写盘失败必须提示错误，不能静默破坏原数据。

## 10. 汇报/答辩重点

1. **结构体与关系**：`Student`/`Lab`/`Reservation` 为什么分开，解决原题一个电脑只存一个姓名的问题。
2. **时间+座位双维度**：`date + slot_index + computer_no` 联合确定座位是否可预约。
3. **角色权限**：学生与管理员功能分开，不允许跨角色执行。
4. **文件持久化**：加载、保存、编号续增、异常处理和重启恢复。
5. **边界测试**：重复预约、取消、故障、跨日统计、输入容错。
6. **统计口径的诚实性**：预约人数与实际到场人数不同；没有签到不要称为“实际上机人数”。

## 11. 最小验收结论

- **优先实现 P0—P3 和每日统计**，确保全部必做功能完成；之后再做选做功能和美化。
- 不引入数据库、浏览器、Web 服务或第三方 UI 框架；保持纯 C、终端菜单和文件操作的课程设计定位。
- 如果老师要求严格使用给定 `struct lab` 的 `eight`～`sixteen` 字段，则保留它们作为指定日期的显示缓存，并解释预约明细仍须由独立结构存储。
- 该方案是工程设计文档，而非已完成代码，进入开发时逐模块落地并测试。
