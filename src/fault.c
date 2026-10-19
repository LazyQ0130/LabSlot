#include "fault.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

/* 功能、参数及返回值：见 fault.h 中 fault_from_storage 的契约。 */
FaultResult fault_from_storage(StorageResult result)
{
    switch (result) {
    case STORAGE_OK: return FAULT_OK;
    case STORAGE_NO_MEMORY: return FAULT_NO_MEMORY;
    case STORAGE_CAPACITY: return FAULT_CAPACITY;
    case STORAGE_RECOVERY_REQUIRED: return FAULT_RECOVERY_REQUIRED;
    case STORAGE_ERROR: return FAULT_STORAGE_ERROR;
    }
    return FAULT_STORAGE_ERROR;
}

/* 功能、参数及返回值：见 fault.h 中 fault_require_role 的契约。 */
FaultResult fault_require_role(const AuthSession *session, UserRole role)
{
    AuthResult result;
    if (!auth_session_has_role(session, role)) { return FAULT_UNAUTHORIZED; }
    if (!storage_writes_allowed()) { return FAULT_RECOVERY_REQUIRED; }
    result = auth_validate_session(session, role);
    if (result == AUTH_OK) { return FAULT_OK; }
    if (result == AUTH_NO_MEMORY) { return FAULT_NO_MEMORY; }
    return result == AUTH_NOT_FOUND ? FAULT_ACCOUNT_NOT_FOUND : FAULT_STORAGE_ERROR;
}

/* 功能、参数及返回值：见 fault.h 中 fault_read_time 的契约。 */
FaultResult fault_read_time(const BookingContext *context, LabTime *now)
{
    int ok = context != NULL && context->now != NULL ? context->now(context->clock_context, now) :
        utils_local_time(NULL, now);
    return ok && utils_time_valid(now) ? FAULT_OK : FAULT_CLOCK_ERROR;
}

/* 功能、参数及返回值：见 fault.h 中 fault_read_computers 的契约。 */
FaultResult fault_read_computers(const AuthSession *session, Lab computers[COMPUTER_COUNT])
{
    FaultResult result;
    if (session == NULL || (session->role != ROLE_STUDENT && session->role != ROLE_ADMIN)) { return FAULT_UNAUTHORIZED; }
    result = fault_require_role(session, session->role);
    if (result != FAULT_OK) { return result; }
    return storage_load_computers(computers) == 1 ? FAULT_OK : FAULT_STORAGE_ERROR;
}

/* 功能、参数及返回值：见 fault.h 中 fault_submit 的契约。 */
FaultResult fault_submit(const BookingContext *context, const AuthSession *session, int no,
                         const char *detail, FaultReport *created)
{
    FaultReportList reports = {0};
    FaultReport draft = {0};
    Lab computers[COMPUTER_COUNT];
    LabTime now = {0};
    size_t i;
    int maximum = 0;
    FaultResult result = fault_require_role(session, ROLE_STUDENT);
    if (result != FAULT_OK) { return result; }
    if (no < 1 || no > COMPUTER_COUNT || created == NULL || !utils_valid_field(detail, DETAIL_LEN)) { return FAULT_INVALID_INPUT; }
    if (storage_load_computers(computers) != 1) { return FAULT_STORAGE_ERROR; }
    if (computers[no-1].status == COMPUTER_FAULT) { return FAULT_ALREADY_FAULT; }
    result = fault_read_time(context, &now);
    if (result != FAULT_OK) { return result; }
    result = fault_from_storage(storage_load_reports(&reports));
    if (result != FAULT_OK) { goto done; }
    for (i = 0; i < reports.count; ++i) {
        const FaultReport *r = &reports.items[i];
        if (r->id > maximum) { maximum = r->id; }
        if (r->computer_no == no && r->status == REPORT_PENDING && strcmp(r->student_id, session->user_id) == 0) {
            result = FAULT_DUPLICATE; goto done;
        }
    }
    if (maximum == INT_MAX) { result = FAULT_ID_EXHAUSTED; goto done; }
    result = fault_from_storage(storage_reserve_reports(&reports, reports.count+1));
    if (result != FAULT_OK) { goto done; }
    draft.id = maximum+1; draft.computer_no = no; draft.date = now.date; draft.status = REPORT_PENDING;
    strcpy(draft.student_id, session->user_id); strcpy(draft.detail, detail);
    reports.items[reports.count++] = draft;
    result = fault_from_storage(storage_save_reports(&reports));
    if (result == FAULT_OK) { *created = draft; }
done:
    storage_free_reports(&reports); return result;
}

/* 功能、参数及返回值：见 fault.h 中 fault_query_reports 的契约。 */
FaultResult fault_query_reports(const AuthSession *session, int no, int status, FaultReportList *out)
{
    FaultReportList reports = {0};
    FaultResult result;
    size_t i, used = 0;
    if (out == NULL || no < 0 || no > COMPUTER_COUNT || status < -1 || status > REPORT_PROCESSED) { return FAULT_INVALID_INPUT; }
    if (session == NULL || (session->role != ROLE_ADMIN && session->role != ROLE_STUDENT)) { return FAULT_UNAUTHORIZED; }
    result = fault_require_role(session, session->role);
    if (result != FAULT_OK) { return result; }
    result = fault_from_storage(storage_load_reports(&reports));
    if (result != FAULT_OK) { return result; }
    for (i = 0; i < reports.count; ++i) {
        const FaultReport *r = &reports.items[i];
        if ((no == 0 || r->computer_no == no) && (status == -1 || (int)r->status == status) &&
            (session->role == ROLE_ADMIN || strcmp(r->student_id, session->user_id) == 0)) {
            reports.items[used++] = *r;
        }
    }
    reports.count = used;
    storage_free_reports(out); *out = reports;
    return FAULT_OK;
}

/* 功能、参数及返回值：见 fault.h 中 fault_get_report 的契约。 */
FaultResult fault_get_report(const AuthSession *session, int id, FaultReport *out)
{
    FaultReportList reports = {0};
    FaultResult result = fault_require_role(session, ROLE_ADMIN);
    size_t i;
    if (result != FAULT_OK) { return result; }
    if (id <= 0 || out == NULL) { return FAULT_INVALID_INPUT; }
    result = fault_from_storage(storage_load_reports(&reports));
    if (result != FAULT_OK) { return result; }
    result = FAULT_NOT_FOUND;
    for (i = 0; i < reports.count; ++i) { if (reports.items[i].id == id) { *out = reports.items[i]; result = FAULT_OK; break; } }
    storage_free_reports(&reports); return result;
}

/* 功能：共同预检维护，允许报告处理时电脑已达目标。参数：目标/输出/allow_same。返回：FaultResult。 */
static FaultResult preview(const BookingContext *context, const AuthSession *session, int no,
                            ComputerStatus target, ReservationList *out, int allow_same)
{
    Lab computers[COMPUTER_COUNT];
    LabTime now = {0};
    ReservationList bookings = {0};
    size_t i, used = 0;
    FaultResult result = fault_require_role(session, ROLE_ADMIN);
    if (result != FAULT_OK) { return result; }
    if (no < 1 || no > COMPUTER_COUNT || out == NULL || (target != COMPUTER_FAULT && target != COMPUTER_NORMAL)) { return FAULT_INVALID_INPUT; }
    if (storage_load_computers(computers) != 1) { return FAULT_STORAGE_ERROR; }
    result = fault_from_storage(storage_load_reservations(&bookings));
    if (result != FAULT_OK) { return result; }
    if (!allow_same && computers[no-1].status == target) {
        result = target == COMPUTER_FAULT ? FAULT_ALREADY_FAULT : FAULT_ALREADY_NORMAL; goto done;
    }
    result = fault_read_time(context, &now);
    if (result != FAULT_OK) { goto done; }
    for (i = 0; i < bookings.count; ++i) {
        const Reservation *r = &bookings.items[i];
        if (target == COMPUTER_FAULT && r->computer_no == no && r->status == BOOKING_ACTIVE &&
            utils_slot_future(&now, r->date, r->slot_index)) { bookings.items[used++] = *r; }
    }
    bookings.count = used;
    storage_free_reservations(out); *out = bookings; memset(&bookings, 0, sizeof bookings);
done:
    storage_free_reservations(&bookings); return result;
}

/* 功能、参数及返回值：见 fault.h 中 fault_preview_change 的契约。 */
FaultResult fault_preview_change(const BookingContext *context, const AuthSession *session, int no,
                                 ComputerStatus target, ReservationList *affected)
{
    return preview(context, session, no, target, affected, 0);
}

/* 功能、参数及返回值：见 fault.h 中 fault_change_computer 的契约。 */
FaultResult fault_change_computer(const BookingContext *context, const AuthSession *session, int no, ComputerStatus target)
{
    ReservationList affected = {0};
    LabTime cutoff = {0};
    FaultResult result = preview(context, session, no, target, &affected, 0);
    storage_free_reservations(&affected);
    if (result == FAULT_OK) { result = fault_read_time(context, &cutoff); }
    if (result == FAULT_OK) { result = fault_from_storage(storage_apply_maintenance(
        target == COMPUTER_FAULT ? STORAGE_MARK_FAULT : STORAGE_RESTORE, no, 0, &cutoff)); }
    return result;
}

/* 功能、参数及返回值：见 fault.h 中 fault_preview_process 的契约。 */
FaultResult fault_preview_process(const BookingContext *context, const AuthSession *session, int id,
                                  FaultAction action, FaultReport *report, ReservationList *affected)
{
    FaultReport found;
    ReservationList empty = {0};
    FaultResult result;
    if (report == NULL || affected == NULL || action < FAULT_CONFIRM || action > FAULT_DISMISS) { return FAULT_INVALID_INPUT; }
    result = fault_get_report(session, id, &found);
    if (result != FAULT_OK) { return result; }
    if (found.status == REPORT_PROCESSED) { return FAULT_ALREADY_PROCESSED; }
    if (action != FAULT_DISMISS) {
        result = preview(context, session, found.computer_no,
                         action == FAULT_CONFIRM ? COMPUTER_FAULT : COMPUTER_NORMAL, affected, 1);
    } else { storage_free_reservations(affected); *affected = empty; }
    if (result == FAULT_OK) { *report = found; }
    return result;
}

/* 功能、参数及返回值：见 fault.h 中 fault_process 的契约。 */
FaultResult fault_process(const BookingContext *context, const AuthSession *session, int id, FaultAction action)
{
    FaultReport report;
    ReservationList affected = {0};
    LabTime cutoff = {0};
    FaultResult result = fault_preview_process(context, session, id, action, &report, &affected);
    storage_free_reservations(&affected);
    if (result == FAULT_OK) { result = fault_read_time(context, &cutoff); }
    if (result == FAULT_OK) { result = fault_from_storage(storage_apply_maintenance(
        (MaintenanceAction)action, report.computer_no, report.id, &cutoff)); }
    return result;
}

const char *fault_result_message(FaultResult result)
{
    switch (result) {
    case FAULT_OK: return "操作成功";
    case FAULT_INVALID_INPUT: return "字段、编号或 UTF-8 无效/超长";
    case FAULT_UNAUTHORIZED: return "需要对应角色登录";
    case FAULT_ACCOUNT_NOT_FOUND: return "账户不存在";
    case FAULT_ALREADY_FAULT: return "电脑已有故障，无需重复操作";
    case FAULT_ALREADY_NORMAL: return "电脑已正常，无需操作";
    case FAULT_DUPLICATE: return "本人对该电脑已有未处理报告";
    case FAULT_NOT_FOUND: return "报告不存在";
    case FAULT_ALREADY_PROCESSED: return "报告已经处理，无需重复操作";
    case FAULT_STORAGE_ERROR: return "文件读取、校验或写入失败";
    case FAULT_CAPACITY: return "记录容量已满";
    case FAULT_NO_MEMORY: return "内存分配失败";
    case FAULT_ID_EXHAUSTED: return "报告编号已耗尽";
    case FAULT_CLOCK_ERROR: return "无法取得有效时间";
    case FAULT_RECOVERY_REQUIRED: return "维护未完成，请重启恢复；当前操作未确认成功";
    }
    return "未知错误";
}
