#ifndef LABSLOT_HEALTH_H
#define LABSLOT_HEALTH_H
#include "fault.h"

/* 集中定义公开的启发式系数；不表示硬件检测或未来故障概率。 */
enum {
    HEALTH_FAULT_PENALTY=65, HEALTH_RECENT_7_PENALTY=8,
    HEALTH_DAYS_8_30_PENALTY=3, HEALTH_OLDER_PENALTY=1,
    HEALTH_PENDING_PENALTY=4, HEALTH_REPEAT_PENALTY=2,
    HEALTH_PENDING_WARNING=2, HEALTH_REPEAT_WARNING=2
};
typedef enum { HEALTH_GOOD, HEALTH_ATTENTION, HEALTH_CHECK, HEALTH_HIGH } HealthLevel;
enum { HEALTH_KNOWN_FAULT=1, HEALTH_MANY_PENDING=2, HEALTH_RECENT_REPEAT=4 };
typedef struct {
    int computer_no;
    ComputerStatus status;
    int score;
    HealthLevel level;
    int recent_7, days_8_30, older, pending, total_reports;
    int report_penalty, fault_penalty, repeat_penalty;
    unsigned int warnings;
} DeviceHealth;
typedef struct {
    DeviceHealth devices[COMPUTER_COUNT]; /* 评分升序，同分编号升序。 */
    int level_counts[4];
    LabTime as_of;
} HealthSummary;

/* day age 0..6 / 7..29 / >=30 为互斥窗口。每条报告仅累计一次总额度，
 * 其中含时间基础扣分、待处理修饰和重复修饰。未来报告日期拒绝整个分析。
 * 仅管理员，严格加载一次电脑/报告；不暴露学生身份或报告说明，不写文件。 */
FaultResult health_compute(const BookingContext *context, const AuthSession *session, HealthSummary *out);
int health_level(int score); /* 有效0..100返回等级，否则-1。 */
const char *health_level_message(HealthLevel level);
#endif
