#ifndef SMART_LAB_MODEL_H
#define SMART_LAB_MODEL_H

#include <stddef.h>

/* 固定规模和开放时间：08:00～17:00，每个时段为一小时。 */
#define LAB_ROWS 5
#define LAB_COLUMNS 8
#define COMPUTER_COUNT (LAB_ROWS * LAB_COLUMNS)
#define OPEN_HOUR 8
#define CLOSE_HOUR 17
#define SLOT_COUNT (CLOSE_HOUR - OPEN_HOUR)

/* 长度以 UTF-8 字节计，包含字符串结尾；姓名最多可容纳 21 个常见汉字。 */
#define ID_LEN 32
#define NAME_LEN 64
#define PASSWORD_LEN 64
#define DETAIL_LEN 256
#define MAX_ADMINS 30
#define MAX_STUDENTS 1000
#define MAX_RESERVATIONS 100000
#define BOOKING_WINDOW_DAYS 7 /* 今天起共七个自然日，即今天至第六天。 */
#define MAX_REPORTS 10000

/* 电脑的持久状态；预约占用独立存储，不属于电脑固定属性。 */
typedef enum {
    COMPUTER_NORMAL = 0, /* 正常。 */
    COMPUTER_FAULT = 1   /* 故障。 */
} ComputerStatus;

/* 预约的生命周期；过去仍有效的记录可作为历史预约。 */
typedef enum {
    BOOKING_CANCELLED = 0, /* 已取消。 */
    BOOKING_ACTIVE = 1     /* 有效。 */
} BookingStatus;

/* 报告状态与电脑状态分离，报障不自动等同于确认故障。 */
typedef enum {
    REPORT_PENDING = 0,  /* 等待管理员处理。 */
    REPORT_PROCESSED = 1 /* 已处理。 */
} ReportStatus;

/* 账户所属角色；不同角色具有独立的标识命名空间。 */
typedef enum {
    ROLE_NONE = 0,    /* 未登录。 */
    ROLE_ADMIN = 1,   /* 管理员。 */
    ROLE_STUDENT = 2  /* 学生。 */
} UserRole;

/* 管理员账户。 */
typedef struct {
    char id[ID_LEN];             /* 唯一工号。 */
    char name[NAME_LEN];         /* UTF-8 姓名。 */
    char password[PASSWORD_LEN]; /* 教学演示明文密码，不是生产安全方案。 */
} Admin;

/* 学生账户与累计预约时长。 */
typedef struct {
    char id[ID_LEN];             /* 唯一学号。 */
    char name[NAME_LEN];         /* UTF-8 姓名。 */
    char password[PASSWORD_LEN]; /* 教学演示密码。 */
    int time;                   /* 已过去且未取消的预约小时数，并非实际签到时间。 */
} Student;

/* 一次操作的一种角色账户集合；role 决定有效成员，由调用方在堆上分配。 */
typedef union {
    Admin admins[MAX_ADMINS];       /* ROLE_ADMIN 时使用。 */
    Student students[MAX_STUDENTS]; /* ROLE_STUDENT 时使用。 */
} AccountRecords;

/* 单台电脑的基础数据，不在这里保存学生或跨时段预约状态。 */
typedef struct {
    int no;                /* 唯一编号，范围 1～40。 */
    ComputerStatus status; /* 正常或故障。 */
} Lab;

/* 一条预约独立表达 日期 + 时段 + 电脑 + 学生 的关系。 */
typedef struct {
    int id;                  /* 预约模块分配的正整数编号，取消后不复用。 */
    char student_id[ID_LEN];  /* 预约人的学号。 */
    int computer_no;          /* 电脑编号 1～40。 */
    int date;                /* YYYYMMDD，例如 20261020。 */
    int slot_index;          /* 0～8，0 表示 08:00～09:00。 */
    BookingStatus status;    /* 有效或已取消。 */
} Reservation;

/* 堆上预约集合，使用前零初始化，使用后 storage_free_reservations。
 * 所有权由持有此结构体的调用方负责，不得浅复制后分别释放。
 */
typedef struct {
    Reservation *items; /* 动态数组，零容量时为 NULL。 */
    size_t count;       /* 已初始化的记录数量。 */
    size_t capacity;    /* 实际分配条数，最多 MAX_RESERVATIONS。 */
} ReservationList;

/* 故障报告；保存于题目指定的 error.txt。 */
typedef struct {
    int id;                 /* 正整数报告编号。 */
    char student_id[ID_LEN]; /* 报告人的学号。 */
    int computer_no;         /* 被报告的电脑编号。 */
    int date;               /* 报告日期 YYYYMMDD。 */
    char detail[DETAIL_LEN]; /* UTF-8 故障说明，拒绝换行及字段分隔符。 */
    ReportStatus status;    /* 待处理或已处理。 */
} FaultReport;

/* 堆上故障报告集合，使用前零初始化，最终 storage_free_reports；禁止浅复制后重复释放。 */
typedef struct {
    FaultReport *items; /* 动态记录数组。 */
    size_t count;      /* 已初始化条数。 */
    size_t capacity;   /* 已分配条数，上限 MAX_REPORTS。 */
} FaultReportList;

#endif
