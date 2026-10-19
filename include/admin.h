#ifndef SMART_LAB_ADMIN_H
#define SMART_LAB_ADMIN_H
#include "fault.h"

/* 查询投影只含公开基本信息，绝不返回密码或未实现的累计时间。 */
typedef struct {
    char student_id[ID_LEN];
    char name[NAME_LEN];
    int computer_no;
    int reservation_id;
} AdminUsageRow;
typedef struct {
    BookingSeats seats;
    size_t count; /* 有效预约人数，不代表签到/实际到场人数。 */
    AdminUsageRow rows[COMPUTER_COUNT];
} AdminUsage;
typedef struct {
    char student_id[ID_LEN];
    char name[NAME_LEN];
    size_t active_count;
    size_t history_count; /* 全部记录数，含有效与已取消。 */
    ReservationList records; /* 调用方最终 admin_free_student。 */
} AdminStudentInfo;

/* 功能：管理员专属只读真实预约查询。参数：会话/日期/时段/成功输出。返回：FaultResult。 */
FaultResult admin_query_usage(const AuthSession *session, int date, int slot, AdminUsage *out);
/* 功能：精确查询学生，无身份冒用。参数：管理员会话/学号/mode 有效或全部/已初始化输出。
 * 返回：FaultResult；失败不改输出。历史总数含有效和取消，Student.time 不读取到结果。
 */
FaultResult admin_query_student(const AuthSession *session, const char *id, BookingQuery mode, AdminStudentInfo *out);
/* 功能：释放学生查询持有的预约并清零。参数：info 可空。返回：无。 */
void admin_free_student(AdminStudentInfo *info);
#endif
