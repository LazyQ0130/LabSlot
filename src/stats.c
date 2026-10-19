#include "stats.h"

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "storage.h"

_Static_assert(MAX_RESERVATIONS <= INT_MAX, "Reservation counters must fit int");
_Static_assert(DAILY_CAPACITY * 7 <= INT_MAX, "Weekly counters must fit int");

/* 功能：将细分存储结果转换为统计结果。参数：result。返回：StatsResult。 */
static StatsResult from_storage(StorageResult result)
{
    switch (result) {
    case STORAGE_OK: return STATS_OK;
    case STORAGE_NO_MEMORY: return STATS_NO_MEMORY;
    case STORAGE_CAPACITY: return STATS_CAPACITY;
    case STORAGE_RECOVERY_REQUIRED: return STATS_RECOVERY_REQUIRED;
    case STORAGE_ERROR: return STATS_STORAGE_ERROR;
    }
    return STATS_STORAGE_ERROR;
}

/* 功能：检查角色/恢复门禁并重读实际账户，学生可输出姓名，不使用缓存时长。
 * 参数：session/role/name可空。返回：StatsResult；不返回凭据。
 */
static StatsResult require_role(const AuthSession *session, UserRole role, char name[NAME_LEN])
{
    AccountRecords *accounts;
    size_t count = 0, i;
    StatsResult result = STATS_ACCOUNT_NOT_FOUND;
    if (!auth_session_has_role(session, role)) { return STATS_UNAUTHORIZED; }
    if (!storage_writes_allowed()) { return STATS_RECOVERY_REQUIRED; }
    accounts = malloc(sizeof *accounts);
    if (accounts == NULL) { return STATS_NO_MEMORY; }
    if (!storage_load_accounts(role, accounts, &count)) { result = STATS_STORAGE_ERROR; }
    else {
        for (i = 0; i < count; ++i) {
            const char *id = role == ROLE_ADMIN ? accounts->admins[i].id : accounts->students[i].id;
            if (strcmp(id, session->user_id) == 0) {
                if (name != NULL) { strcpy(name, role == ROLE_ADMIN ? accounts->admins[i].name : accounts->students[i].name); }
                result = STATS_OK; break;
            }
        }
    }
    utils_clear_secret(accounts, sizeof *accounts); free(accounts);
    return result;
}

/* 功能：取一次并校验时钟。参数：context可空/now输出。返回：StatsResult。 */
static StatsResult read_time(const BookingContext *context, LabTime *now)
{
    int ok = context != NULL && context->now != NULL ? context->now(context->clock_context, now) : utils_local_time(NULL, now);
    return ok && utils_time_valid(now) ? STATS_OK : STATS_CLOCK_ERROR;
}

/* 功能：按学号、日期排序预约索引。参数：a/b索引项。返回：排序顺序。 */
static int student_date_order(const void *a, const void *b)
{
    const Reservation *x = *(const Reservation *const *)a;
    const Reservation *y = *(const Reservation *const *)b;
    int order = strcmp(x->student_id, y->student_id);
    return order != 0 ? order : (x->date > y->date) - (x->date < y->date);
}

/* 功能：一次完整加载、遍历聚合，再对最多2520个有效项排序一次去重。
 * 参数：时钟/管理员会话/start/长度1或7/成功输出。返回：StatsResult，不写任何文件。
 */
static StatsResult compute_range(const BookingContext *context, const AuthSession *session,
                                  int start, int days, WeeklyStats *out)
{
    WeeklyStats result = {0};
    ReservationList reservations = {0};
    const Reservation **selected = NULL;
    LabTime now = {0};
    StatsResult status;
    size_t i, used = 0, capacity;
    int day, start_serial = utils_date_serial(start);
    if ((days != 1 && days != 7) || out == NULL || start_serial < 0 || utils_date_add(start, days-1) == 0) { return STATS_INVALID_INPUT; }
    status = require_role(session, ROLE_ADMIN, NULL);
    if (status != STATS_OK) { return status; }
    status = read_time(context, &now);
    if (status != STATS_OK) { return status; }
    result.start_date = start; result.end_date = utils_date_add(start, days-1);
    for (day = 0; day < days; ++day) {
        result.days[day].counts.date = utils_date_add(start, day);
        result.days[day].as_of = now;
    }
    status = from_storage(storage_load_reservations(&reservations));
    if (status != STATS_OK) { goto done; }
    capacity = reservations.count < (size_t)(DAILY_CAPACITY * days) ? reservations.count : (size_t)(DAILY_CAPACITY * days);
    if (capacity > SIZE_MAX / sizeof *selected) { status = STATS_CAPACITY; goto done; }
    if (capacity > 0) {
        selected = malloc(capacity * sizeof *selected);
        if (selected == NULL) { status = STATS_NO_MEMORY; goto done; }
    }
    for (i = 0; i < reservations.count; ++i) {
        const Reservation *r = &reservations.items[i];
        DailyStats *daily;
        if (r->status != BOOKING_ACTIVE || r->date < start || r->date > result.end_date) { continue; }
        day = utils_date_serial(r->date) - start_serial;
        if (day < 0 || day >= days || used >= capacity) { status = STATS_STORAGE_ERROR; goto done; }
        daily = &result.days[day];
        ++daily->counts.reservation_count; ++daily->counts.reserved_hours;
        ++daily->counts.slot_counts[r->slot_index]; ++result.reservation_count;
        if (utils_slot_ended(&now, r->date, r->slot_index)) { ++daily->completed_hours; }
        selected[used++] = r;
    }
    if (used > 1) { qsort(selected, used, sizeof *selected, student_date_order); }
    for (i = 0; i < used; ++i) {
        int new_student = i == 0 || strcmp(selected[i-1]->student_id, selected[i]->student_id) != 0;
        day = utils_date_serial(selected[i]->date) - start_serial;
        if (new_student) { ++result.student_count; }
        if (new_student || selected[i-1]->date != selected[i]->date) { ++result.days[day].counts.student_count; }
    }
    for (day = 0; day < days; ++day) {
        int slot;
        DailyStats *daily = &result.days[day];
        daily->utilization = daily->counts.reserved_hours * 100.0 / DAILY_CAPACITY;
        for (slot = 0; slot < SLOT_COUNT; ++slot) { daily->slot_utilization[slot] = daily->counts.slot_counts[slot] * 100.0 / COMPUTER_COUNT; }
    }
    result.average_daily_hours = result.reservation_count / (double)days;
    result.utilization = result.reservation_count * 100.0 / (DAILY_CAPACITY * days);
    *out = result;
done:
    free(selected); storage_free_reservations(&reservations); return status;
}

/* 功能、参数及返回值见stats.h的日报契约。 */
StatsResult stats_compute_daily(const BookingContext *context, const AuthSession *session, int date, DailyStats *out)
{
    WeeklyStats result;
    StatsResult status;
    if (!utils_valid_date(date) || out == NULL) { return STATS_INVALID_INPUT; }
    status = compute_range(context, session, date, 1, &result);
    if (status == STATS_OK) { *out = result.days[0]; }
    return status;
}

/* 功能、参数及返回值见stats.h的周报契约。 */
StatsResult stats_compute_week(const BookingContext *context, const AuthSession *session, int date, WeeklyStats *out)
{
    int start;
    if (!utils_valid_date(date) || out == NULL) { return STATS_INVALID_INPUT; }
    /* 日序1为公历0001-01-01周一；只使用公历工具，不对YYYYMMDD直接加减。 */
    start = utils_date_add(date, -((utils_date_serial(date)-1) % 7));
    if (start == 0 || utils_date_add(start, 6) == 0) { return STATS_DATE_RANGE; }
    return compute_range(context, session, start, 7, out);
}

/* 功能、参数及返回值见stats.h的个人累计契约。 */
StatsResult stats_query_student(const BookingContext *context, const AuthSession *session, StudentStats *out)
{
    StudentStats result = {0};
    ReservationList reservations = {0};
    StatsResult status;
    size_t i;
    if (out == NULL) { return STATS_INVALID_INPUT; }
    status = require_role(session, ROLE_STUDENT, result.name);
    if (status != STATS_OK) { return status; }
    status = read_time(context, &result.as_of);
    if (status != STATS_OK) { return status; }
    strcpy(result.student_id, session->user_id);
    status = from_storage(storage_load_reservations(&reservations));
    if (status != STATS_OK) { return status; }
    /* 全部计数最大MAX_RESERVATIONS，由上方静态断言及完整加载上限保证int不溢出。 */
    for (i = 0; i < reservations.count; ++i) {
        const Reservation *r = &reservations.items[i];
        if (strcmp(r->student_id, session->user_id) != 0) { continue; }
        if (r->status == BOOKING_CANCELLED) { ++result.cancelled_count; }
        else {
            ++result.active_count;
            if (utils_slot_ended(&result.as_of, r->date, r->slot_index)) { ++result.completed_count; }
            else { ++result.pending_count; }
        }
    }
    result.completed_hours = result.completed_count; /* 一条恰为一小时，不累加旧缓存。 */
    storage_free_reservations(&reservations); *out = result; return STATS_OK;
}

/* 功能、参数及返回值见stats.h的授权导出契约。 */
StatsResult stats_export_daily(const BookingContext *context, const AuthSession *session, int date, DailyStats *out)
{
    DailyStats daily;
    DailyCountList counts = {0};
    StatsResult status;
    size_t position = 0;
    if (out == NULL) { return STATS_INVALID_INPUT; }
    status = stats_compute_daily(context, session, date, &daily);
    if (status != STATS_OK) { return status; }
    status = from_storage(storage_load_daily_counts(&counts));
    if (status != STATS_OK) { goto done; }
    while (position < counts.count && counts.items[position].date < date) { ++position; }
    if (position < counts.count && counts.items[position].date == date) { counts.items[position] = daily.counts; }
    else {
        status = from_storage(storage_reserve_daily_counts(&counts, counts.count+1));
        if (status != STATS_OK) { goto done; }
        memmove(&counts.items[position+1], &counts.items[position], (counts.count-position) * sizeof *counts.items);
        counts.items[position] = daily.counts; ++counts.count;
    }
    status = from_storage(storage_save_daily_counts(&counts));
    if (status == STATS_OK) { *out = daily; }
done:
    storage_free_daily_counts(&counts); return status;
}

/* 功能：统计结果说明，不包含凭据。参数及返回值见stats.h。 */
const char *stats_result_message(StatsResult result)
{
    switch (result) {
    case STATS_OK: return "操作成功";
    case STATS_INVALID_INPUT: return "日期或参数无效";
    case STATS_UNAUTHORIZED: return "需要对应角色登录";
    case STATS_ACCOUNT_NOT_FOUND: return "当前角色账户不存在";
    case STATS_STORAGE_ERROR: return "来源/统计文件损坏、读取或保存失败";
    case STATS_NO_MEMORY: return "内存分配失败";
    case STATS_CAPACITY: return "记录容量超过上限";
    case STATS_CLOCK_ERROR: return "无法取得有效本地时间";
    case STATS_RECOVERY_REQUIRED: return "维护未完成，请先重启恢复，再查询或导出统计";
    case STATS_DATE_RANGE: return "该日期所在完整周超出1000～9999年支持范围";
    }
    return "未知统计错误";
}
