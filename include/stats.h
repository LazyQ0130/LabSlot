#ifndef SMART_LAB_STATS_H
#define SMART_LAB_STATS_H

#include "booking.h"

typedef enum {
    STATS_OK = 0, STATS_INVALID_INPUT, STATS_UNAUTHORIZED, STATS_ACCOUNT_NOT_FOUND,
    STATS_STORAGE_ERROR, STATS_NO_MEMORY, STATS_CAPACITY, STATS_CLOCK_ERROR,
    STATS_RECOVERY_REQUIRED, STATS_DATE_RANGE
} StatsResult;

/* 实时日报；导出仅保存counts，完成小时与利用率每次重新推导。 */
typedef struct {
    DailyCount counts;
    int completed_hours;              /* 已结束有效预约小时数，包含结束时刻相等。 */
    double utilization;               /* 百分数，0～100，分母固定360。 */
    double slot_utilization[SLOT_COUNT]; /* 百分数，分母固定40。 */
    LabTime as_of;                    /* 本次计算唯一时刻。 */
} DailyStats;

typedef struct {
    int start_date, end_date;         /* 周一至周日，完整七天必须在支持日期范围内。 */
    DailyStats days[7];               /* 同一来源快照、同一时刻的七天。 */
    int reservation_count;            /* 同时等于整周预约小时数，最多2520。 */
    int student_count;                /* 整周按学号去重，不能相加每日人数。 */
    double average_daily_hours;       /* 预约小时数/7。 */
    double utilization;               /* 预约小时数/2520*100%。 */
} WeeklyStats;

typedef struct {
    char student_id[ID_LEN];          /* 仅当前会话学生。 */
    char name[NAME_LEN];
    int completed_hours;             /* 权威动态值，不读取Student.time缓存。 */
    int active_count, completed_count, pending_count, cancelled_count;
    LabTime as_of;
} StudentStats;

/* 功能：管理员实时日报，不写文件。参数：context可空系统时钟/session/date/成功输出。
 * 返回：StatsResult；失败不修改输出，全部结果只代表预约事实。
 */
StatsResult stats_compute_daily(const BookingContext *context, const AuthSession *session, int date, DailyStats *out);
/* 功能：任意日期定位完整周一至周日，一次加载/聚合，实时只读。
 * 参数：时钟/管理员会话/日期/成功输出。返回：StatsResult；跨支持日期边界拒绝，不输出残周。
 */
StatsResult stats_compute_week(const BookingContext *context, const AuthSession *session, int date, WeeklyStats *out);
/* 学生/管理员共享的周聚合投影，不含身份明细；与日报/周报同一聚合实现。 */
StatsResult stats_compute_heatmap(const BookingContext *context, const AuthSession *session, int date, WeeklyStats *out);
/* 功能：仅当前学生累计已结束的有效预约；无指定他人学号参数，不写账户文件。
 * 参数：时钟/学生会话/成功输出。返回：StatsResult；失败保持输出不变。
 */
StatsResult stats_query_student(const BookingContext *context, const AuthSession *session, StudentStats *out);
/* 功能：管理员显式导出所选日期，重新计算并upsert有序count.txt，保留其他日期。
 * 参数：时钟/管理员会话/日期/持久化成功才发布的日报输出。返回：StatsResult。
 * 取代旧无会话占位接口；无越权兼容入口，失败不修改旧目标或输出。
 */
StatsResult stats_export_daily(const BookingContext *context, const AuthSession *session, int date, DailyStats *out);
/* 功能：无凭据的结果说明。参数：result。返回：静态文本。 */
const char *stats_result_message(StatsResult result);
#endif
