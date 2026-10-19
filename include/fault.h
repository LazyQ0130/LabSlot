#ifndef SMART_LAB_FAULT_H
#define SMART_LAB_FAULT_H

#include "booking.h"
#include "storage.h"

typedef enum {
    FAULT_OK = 0, FAULT_INVALID_INPUT, FAULT_UNAUTHORIZED, FAULT_ACCOUNT_NOT_FOUND,
    FAULT_ALREADY_FAULT, FAULT_ALREADY_NORMAL, FAULT_DUPLICATE, FAULT_NOT_FOUND,
    FAULT_ALREADY_PROCESSED, FAULT_STORAGE_ERROR, FAULT_CAPACITY, FAULT_NO_MEMORY,
    FAULT_ID_EXHAUSTED, FAULT_CLOCK_ERROR, FAULT_RECOVERY_REQUIRED
} FaultResult;

typedef enum {
    FAULT_CONFIRM = 1, /* 确认故障并取消未开始预约。 */
    FAULT_REPAIRED = 2, /* 恢复电脑并关闭报告。 */
    FAULT_DISMISS = 3 /* 仅关闭报告。 */
} FaultAction;

/* 功能：共享业务权限门禁，重读实际账户且检查恢复状态。参数：会话/角色。返回：FaultResult。 */
FaultResult fault_require_role(const AuthSession *session, UserRole role);
/* 功能：转换存储结果。参数：result。返回：FaultResult。 */
FaultResult fault_from_storage(StorageResult result);
/* 功能：验证时钟。参数：context 可空；now 成功输出。返回：FaultResult。 */
FaultResult fault_read_time(const BookingContext *context, LabTime *now);
/* 功能：读取当前电脑，学生/管理员均需真实账户。参数：会话/输出40台。返回：FaultResult。 */
FaultResult fault_read_computers(const AuthSession *session, Lab computers[COMPUTER_COUNT]);
/* 功能：以会话学生身份提交，持久化成功才发布报告；不改电脑/预约。
 * 参数：时钟/学生会话/电脑/说明/成功输出。返回：FaultResult。
 */
FaultResult fault_submit(const BookingContext *context, const AuthSession *session, int no,
                         const char *detail, FaultReport *created);
/* 功能：查询报告，学生仅本人，管理员全部。参数：no=0 全部；status=-1 全部；输出自有集合。
 * 返回：FaultResult；成功替换输出，调用方 storage_free_reports。
 */
FaultResult fault_query_reports(const AuthSession *session, int no, int status, FaultReportList *out);
/* 功能：管理员取得报告详情。参数：会话/id/成功输出。返回：FaultResult。 */
FaultResult fault_get_report(const AuthSession *session, int id, FaultReport *out);
/* 功能：管理员维护预览，返回受影响的未来预约，不写文件。
 * 参数：时钟/会话/电脑/目标状态/自有输出集合。返回：FaultResult。
 */
FaultResult fault_preview_change(const BookingContext *context, const AuthSession *session, int no,
                                 ComputerStatus target, ReservationList *affected);
/* 功能：确认后重新验证并通过恢复日志变更电脑。参数：同预览。返回：FaultResult。 */
FaultResult fault_change_computer(const BookingContext *context, const AuthSession *session,
                                  int no, ComputerStatus target);
/* 功能：报告处理预览，不写文件。参数：时钟/会话/id/动作/报告输出/影响预约输出。返回：FaultResult。 */
FaultResult fault_preview_process(const BookingContext *context, const AuthSession *session, int id,
                                  FaultAction action, FaultReport *report, ReservationList *affected);
/* 功能：确认后通过同一恢复日志处理报告，报告状态最后更新。参数：时钟/会话/id/动作。返回：FaultResult。 */
FaultResult fault_process(const BookingContext *context, const AuthSession *session, int id, FaultAction action);
/* 功能：无凭据结果说明。参数：result。返回：静态文本。 */
const char *fault_result_message(FaultResult result);
#endif
