#include "booking.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include "lab.h"
#include "storage.h"

/* 功能：转换存储结果。参数：result。返回：业务结果，不混淆损坏与空表。 */
static BookingResult from_storage(StorageResult result)
{
    switch (result) {
    case STORAGE_OK: return BOOKING_OK;
    case STORAGE_NO_MEMORY: return BOOKING_NO_MEMORY;
    case STORAGE_CAPACITY: return BOOKING_CAPACITY;
    case STORAGE_ERROR:
    case STORAGE_RECOVERY_REQUIRED: return BOOKING_STORAGE_ERROR;
    }
    return BOOKING_STORAGE_ERROR;
}

/* 功能：统一取得并验证时钟。参数：context 可空；now 输出。返回：成功 1，否则 0。 */
static int read_now(const BookingContext *context, LabTime *now)
{
    int success = context != NULL && context->now != NULL ?
        context->now(context->clock_context, now) : utils_local_time(NULL, now); /* 唯一时间来源。 */
    return success && utils_valid_date(now->date) && now->hour >= 0 && now->hour <= 23 &&
        now->minute >= 0 && now->minute <= 59 && now->second >= 0 && now->second <= 59;
}

/* 功能：基于固定快照检查开始时刻。参数：now 当前时间；date/slot 目标；window 限窗口。
 * 返回：业务结果；已经到达开始时刻也视为过期。
 */
static BookingResult check_time_at(const LabTime *now, int date, int slot, int window)
{
    int difference; /* 自然日之差，不直接相减 YYYYMMDD。 */
    if (!utils_valid_date(date) || slot < 0 || slot >= SLOT_COUNT) { return BOOKING_INVALID_INPUT; }
    difference = utils_date_serial(date) - utils_date_serial(now->date);
    if (difference < 0 || (difference == 0 && now->hour * 3600 + now->minute * 60 + now->second >=
                           (OPEN_HOUR + slot) * 3600)) { return BOOKING_TIME_EXPIRED; }
    if (window && difference >= BOOKING_WINDOW_DAYS) { return BOOKING_WINDOW_EXCEEDED; }
    return BOOKING_OK;
}

/* 功能：时间规则检查；参数、返回值见 booking.h。 */
BookingResult booking_check_time(const BookingContext *context, int date, int slot, int window)
{
    LabTime now = {0}; /* 一次操作的时钟快照。 */
    if (!utils_valid_date(date) || slot < 0 || slot >= SLOT_COUNT) { return BOOKING_INVALID_INPUT; }
    if (!read_now(context, &now)) { return BOOKING_CLOCK_ERROR; }
    return check_time_at(&now, date, slot, window);
}

/* 功能：业务边界重新验证学生会话和完整学生表。参数：session。返回：业务结果。 */
static BookingResult require_student(const AuthSession *session)
{
    AccountRecords *accounts; /* 避免在栈上放大型账户集合。 */
    size_t count = 0, i; /* 学生数量与查找下标。 */
    BookingResult result = BOOKING_STUDENT_NOT_FOUND; /* 仅成功读取后允许未找到。 */
    if (!auth_session_has_role(session, ROLE_STUDENT)) { return BOOKING_UNAUTHORIZED; }
    if (!storage_writes_allowed()) { return BOOKING_STORAGE_ERROR; }
    accounts = malloc(sizeof *accounts);
    if (accounts == NULL) { return BOOKING_NO_MEMORY; }
    if (!storage_load_accounts(ROLE_STUDENT, accounts, &count)) {
        result = BOOKING_STORAGE_ERROR;
    } else {
        for (i = 0; i < count; ++i) {
            if (strcmp(accounts->students[i].id, session->user_id) == 0) { result = BOOKING_OK; break; }
        }
    }
    utils_clear_secret(accounts, sizeof *accounts);
    free(accounts);
    return result;
}

/* 功能：两种冲突检查；参数、返回值见 booking.h。 */
BookingResult booking_check_conflict(const ReservationList *list, const char *student_id,
                                    int date, int slot, int no)
{
    size_t i; /* 记录下标。 */
    int student_conflict = 0; /* 先确认同座位冲突，再返回学生冲突。 */
    if (list == NULL || list->count > list->capacity || list->capacity > MAX_RESERVATIONS ||
        (list->count > 0 && list->items == NULL) || !utils_valid_id(student_id) ||
        !utils_valid_date(date) || slot < 0 || slot >= SLOT_COUNT || no < 1 || no > COMPUTER_COUNT) {
        return BOOKING_INVALID_INPUT;
    }
    for (i = 0; i < list->count; ++i) {
        const Reservation *r = &list->items[i]; /* 加载时已经完整验证的记录。 */
        if (r->status == BOOKING_ACTIVE && r->date == date && r->slot_index == slot) {
            if (r->computer_no == no) { return BOOKING_SEAT_OCCUPIED; }
            if (strncmp(r->student_id, student_id, ID_LEN) == 0) { student_conflict = 1; }
        }
    }
    return student_conflict ? BOOKING_STUDENT_CONFLICT : BOOKING_OK;
}

/* 功能：创建时共同加载与预检。参数：context/session/目标；list 自有集合输出；next_id 编号输出。
 * 返回：业务结果；失败时调用方仍负责释放 list。
 */
static BookingResult prepare_create(const BookingContext *context, const AuthSession *session,
                                     int date, int slot, int no, ReservationList *list, int *next_id)
{
    BookingResult result = require_student(session); /* 不能仅依赖 UI。 */
    Lab computers[COMPUTER_COUNT]; /* 小型基础电脑表，从磁盘重新加载。 */
    size_t i; /* 扫描最大编号。 */
    int maximum = 0; /* 包括取消记录在内的最大历史编号，禁止复用。 */
    if (result != BOOKING_OK) { return result; }
    if (no < 1 || no > COMPUTER_COUNT) { return BOOKING_INVALID_INPUT; }
    result = booking_check_time(context, date, slot, 1);
    if (result != BOOKING_OK) { return result; }
    if (storage_load_computers(computers) != 1) { return BOOKING_STORAGE_ERROR; }
    if (computers[no - 1].status == COMPUTER_FAULT) { return BOOKING_COMPUTER_FAULT; }
    result = from_storage(storage_load_reservations(list));
    if (result != BOOKING_OK) { return result; }
    result = booking_check_conflict(list, session->user_id, date, slot, no);
    if (result != BOOKING_OK) { return result; }
    if (list->count >= MAX_RESERVATIONS) { return BOOKING_CAPACITY; }
    for (i = 0; i < list->count; ++i) {
        if (list->items[i].id > maximum) { maximum = list->items[i].id; }
    }
    if (maximum == INT_MAX) { return BOOKING_ID_EXHAUSTED; }
    *next_id = maximum + 1;
    return BOOKING_OK;
}

/* 功能：创建的只读预检；参数、返回值见 booking.h。 */
BookingResult booking_check_create(const BookingContext *context, const AuthSession *session,
                                  int date, int slot, int no)
{
    ReservationList list = {0}; /* 本次预检读取的快照。 */
    int next_id; /* 仅用于确认编号仍有空间，不占用编号。 */
    BookingResult result = prepare_create(context, session, date, slot, no, &list, &next_id);
    storage_free_reservations(&list);
    return result;
}

/* 功能：重新加载并创建预约；参数、返回值见 booking.h。 */
BookingResult booking_create(const BookingContext *context, const AuthSession *session,
                            int date, int slot, int no, Reservation *created)
{
    ReservationList list = {0}; /* 候选全量快照，无长期缓存。 */
    Reservation candidate = {0}; /* 持久化成功前不交给调用者。 */
    BookingResult result; /* 最终结果。 */
    if (created == NULL) { return BOOKING_INVALID_INPUT; }
    result = prepare_create(context, session, date, slot, no, &list, &candidate.id);
    if (result != BOOKING_OK) { goto done; }
    result = from_storage(storage_reserve_reservations(&list, list.count + 1));
    if (result != BOOKING_OK) { goto done; }
    strcpy(candidate.student_id, session->user_id);
    candidate.computer_no = no; candidate.date = date; candidate.slot_index = slot;
    candidate.status = BOOKING_ACTIVE;
    list.items[list.count++] = candidate;
    result = booking_check_time(context, date, slot, 1); /* 大文件读取后在写入边界重查。 */
    if (result == BOOKING_OK) { result = from_storage(storage_save_reservations(&list)); }
    if (result == BOOKING_OK) { *created = candidate; }
done:
    storage_free_reservations(&list);
    return result;
}

/* 功能：加载并检查可取消记录。参数：context/session/id；list 自有集合；index 成功下标。
 * 返回：业务结果；失败时调用方仍负责释放 list。
 */
static BookingResult prepare_cancel(const BookingContext *context, const AuthSession *session,
                                     int id, ReservationList *list, size_t *index)
{
    BookingResult result = require_student(session); /* 必须重新确认学生存在。 */
    size_t i; /* 查找预约编号。 */
    if (result != BOOKING_OK) { return result; }
    if (id <= 0) { return BOOKING_INVALID_INPUT; }
    result = from_storage(storage_load_reservations(list));
    if (result != BOOKING_OK) { return result; }
    for (i = 0; i < list->count; ++i) {
        Reservation *r = &list->items[i]; /* 不先更改记录。 */
        if (r->id != id) { continue; }
        if (strcmp(r->student_id, session->user_id) != 0) { return BOOKING_NOT_OWNER; }
        if (r->status == BOOKING_CANCELLED) { return BOOKING_ALREADY_CANCELLED; }
        result = booking_check_time(context, r->date, r->slot_index, 0); /* 不限七日，只要求未开始。 */
        if (result == BOOKING_OK) { *index = i; }
        return result;
    }
    return BOOKING_NOT_FOUND;
}

/* 功能：取消前只读预检；参数、返回值见 booking.h。 */
BookingResult booking_check_cancel(const BookingContext *context, const AuthSession *session,
                                  int id, Reservation *record)
{
    ReservationList list = {0}; /* 预检快照。 */
    size_t index; /* 完整验证后才能使用的下标。 */
    BookingResult result; /* 结果。 */
    if (record == NULL) { return BOOKING_INVALID_INPUT; }
    result = prepare_cancel(context, session, id, &list, &index);
    if (result == BOOKING_OK) { *record = list.items[index]; }
    storage_free_reservations(&list);
    return result;
}

/* 功能：取消本人尚未开始的有效预约；参数、返回值见 booking.h。 */
BookingResult booking_cancel(const BookingContext *context, const AuthSession *session, int id)
{
    ReservationList list = {0}; /* 局部候选，不删除记录。 */
    size_t index; /* 可取消记录位置。 */
    BookingResult result = prepare_cancel(context, session, id, &list, &index);
    if (result == BOOKING_OK) {
        list.items[index].status = BOOKING_CANCELLED;
        result = booking_check_time(context, list.items[index].date, list.items[index].slot_index, 0);
        if (result == BOOKING_OK) { result = from_storage(storage_save_reservations(&list)); }
    }
    storage_free_reservations(&list);
    return result;
}

/* 功能：个人预约按日期、时段、编号排序。参数：left/right 记录。返回：排序顺序。 */
static int query_order(const void *left, const void *right)
{
    const Reservation *a = left, *b = right; /* 待比较记录，不用减法避免编号溢出。 */
    if (a->date != b->date) { return (a->date > b->date) - (a->date < b->date); }
    if (a->slot_index != b->slot_index) { return (a->slot_index > b->slot_index) - (a->slot_index < b->slot_index); }
    return (a->id > b->id) - (a->id < b->id);
}

/* 功能：过滤本人记录、排序且不写文件；参数、返回值见 booking.h。 */
BookingResult booking_query(const BookingContext *context, const AuthSession *session,
                           BookingQuery mode, ReservationList *result)
{
    ReservationList all = {0}, selected = {0}; /* 独立源快照和结果，失败不污染旧输出。 */
    BookingResult status = require_student(session); /* 会话与学生验证。 */
    LabTime now = {0}; /* 可取消筛选只取一次时间快照。 */
    size_t i; /* 遍历所有记录。 */
    if (status != BOOKING_OK) { return status; }
    if (result == NULL || mode < BOOKING_QUERY_ACTIVE || mode > BOOKING_QUERY_CANCELLABLE) {
        return BOOKING_INVALID_INPUT;
    }
    if (mode == BOOKING_QUERY_CANCELLABLE && !read_now(context, &now)) { return BOOKING_CLOCK_ERROR; }
    status = from_storage(storage_load_reservations(&all));
    if (status != BOOKING_OK) { goto done; }
    for (i = 0; i < all.count; ++i) {
        const Reservation *r = &all.items[i]; /* 只选择当前学生。 */
        if (strcmp(r->student_id, session->user_id) != 0 ||
            (mode != BOOKING_QUERY_ALL && r->status != BOOKING_ACTIVE) ||
            (mode == BOOKING_QUERY_CANCELLABLE && check_time_at(&now, r->date, r->slot_index, 0) != BOOKING_OK)) {
            continue;
        }
        status = from_storage(storage_reserve_reservations(&selected, selected.count + 1));
        if (status != BOOKING_OK) { goto done; }
        selected.items[selected.count++] = *r;
    }
    if (selected.count > 1) { qsort(selected.items, selected.count, sizeof *selected.items, query_order); }
    storage_free_reservations(result);
    *result = selected; memset(&selected, 0, sizeof selected); /* 转交所有权。 */
done:
    storage_free_reservations(&selected); storage_free_reservations(&all);
    return status;
}

/* 功能：重新加载指定视图，故障优先；参数、返回值见 booking.h。 */
BookingResult booking_get_seats(int date, int slot, BookingSeats *seats)
{
    if (!storage_writes_allowed()) { return BOOKING_STORAGE_ERROR; }
    Lab computers[COMPUTER_COUNT]; /* 当前真实电脑状态。 */
    ReservationList list = {0}; /* 当前真实预约。 */
    BookingSeats view = {0}; /* 成功后才发布。 */
    BookingResult result; /* 读取结果。 */
    size_t i; /* 遍历记录和座位。 */
    if (!utils_valid_date(date) || slot < 0 || slot >= SLOT_COUNT || seats == NULL) { return BOOKING_INVALID_INPUT; }
    if (storage_load_computers(computers) != 1) { return BOOKING_STORAGE_ERROR; }
    result = from_storage(storage_load_reservations(&list));
    if (result != BOOKING_OK) { return result; }
    for (i = 0; i < COMPUTER_COUNT; ++i) { view.symbols[i] = lab_seat_symbol(&computers[i]); }
    for (i = 0; i < list.count; ++i) {
        const Reservation *r = &list.items[i]; /* 只计算当前日期和时段的有效预约。 */
        if (r->status == BOOKING_ACTIVE && r->date == date && r->slot_index == slot &&
            view.symbols[r->computer_no - 1] != '!') { view.symbols[r->computer_no - 1] = '#'; }
    }
    for (i = 0; i < COMPUTER_COUNT; ++i) {
        if (view.symbols[i] == '!') { ++view.fault_count; }
        else if (view.symbols[i] == '#') { ++view.booked_count; }
        else { ++view.free_count; }
    }
    *seats = view;
    storage_free_reservations(&list);
    return BOOKING_OK;
}

/* 功能：业务结果转换为可解释提示；参数、返回值见 booking.h。 */
const char *booking_result_message(BookingResult result)
{
    switch (result) {
    case BOOKING_OK: return "操作成功";
    case BOOKING_INVALID_INPUT: return "日期、时段、电脑编号或参数无效";
    case BOOKING_UNAUTHORIZED: return "需要学生登录权限";
    case BOOKING_STUDENT_NOT_FOUND: return "学生账户不存在";
    case BOOKING_COMPUTER_FAULT: return "电脑故障，不能预约";
    case BOOKING_SEAT_OCCUPIED: return "该座位在此日期和时段已预约";
    case BOOKING_STUDENT_CONFLICT: return "你在此日期和时段已有其他预约";
    case BOOKING_NOT_FOUND: return "预约编号不存在";
    case BOOKING_NOT_OWNER: return "不能取消他人的预约";
    case BOOKING_ALREADY_CANCELLED: return "预约已经取消";
    case BOOKING_TIME_EXPIRED: return "预约时段已经开始或日期已过去";
    case BOOKING_WINDOW_EXCEEDED: return "超出预约窗口：仅限今天至第六天";
    case BOOKING_STORAGE_ERROR: return "数据文件损坏、读取或保存失败";
    case BOOKING_CAPACITY: return "预约记录已达到容量上限";
    case BOOKING_NO_MEMORY: return "内存分配失败，请重试";
    case BOOKING_ID_EXHAUSTED: return "预约编号已达整数上限，不能再创建";
    case BOOKING_CLOCK_ERROR: return "无法读取有效的本地时间";
    case BOOKING_RECOVERY_REQUIRED: return "维护未完成，请先重启恢复";
    }
    return "未知预约错误";
}

unsigned int booking_range_mask(int start, int duration)
{
    if (start < 0 || start >= SLOT_COUNT || duration < 1 || duration > 3 || duration > SLOT_COUNT-start) { return 0; }
    return ((1u << duration)-1u) << start;
}

/* 只处理已经严格验证的快照；供推荐及连续创建共同使用。 */
static BookingResult availability_at(const ReservationList *list, const Lab computers[COMPUTER_COUNT],
                                      const char *student, const LabTime *now, int date, BookingAvailability *out)
{
    BookingAvailability view = {0};
    unsigned int time_mask = 0;
    int no, slot, difference = utils_date_serial(date)-utils_date_serial(now->date);
    size_t i;
    if (difference < 0) { return BOOKING_TIME_EXPIRED; }
    if (difference >= BOOKING_WINDOW_DAYS) { return BOOKING_WINDOW_EXCEEDED; }
    for (slot=0; slot<SLOT_COUNT; ++slot) {
        if (utils_slot_future(now,date,slot)) { time_mask |= 1u << slot; }
    }
    for (no=0; no<COMPUTER_COUNT; ++no) {
        view.statuses[no]=computers[no].status;
        view.masks[no] = computers[no].status == COMPUTER_FAULT ? 0 : time_mask;
    }
    for (i=0; i<list->count; ++i) {
        const Reservation *r = &list->items[i];
        if (r->status != BOOKING_ACTIVE || r->date != date) { continue; }
        view.masks[r->computer_no-1] &= ~(1u << r->slot_index);
        ++view.loads[r->computer_no-1];
        if (!strcmp(student,r->student_id)) { view.own_conflicts |= 1u << r->slot_index; }
    }
    for (no=0; no<COMPUTER_COUNT; ++no) { view.masks[no] &= ~view.own_conflicts; }
    view.as_of=*now; *out=view; return BOOKING_OK;
}

BookingResult booking_availability(const BookingContext *context, const AuthSession *session,
                                   int date, BookingAvailability *out)
{
    Lab computers[COMPUTER_COUNT];
    LabTime now;
    ReservationList list = {0};
    BookingResult result;
    if (!auth_session_has_role(session,ROLE_STUDENT)) { return BOOKING_UNAUTHORIZED; }
    if (!storage_writes_allowed()) { return BOOKING_RECOVERY_REQUIRED; }
    result=require_student(session);
    if (result!=BOOKING_OK) { return result; }
    if (!out || !utils_valid_date(date)) { return BOOKING_INVALID_INPUT; }
    if (!read_now(context,&now)) { return BOOKING_CLOCK_ERROR; }
    if (storage_load_computers(computers)!=1) { return BOOKING_STORAGE_ERROR; }
    result=from_storage(storage_load_reservations(&list));
    if (result==BOOKING_OK) { result=availability_at(&list,computers,session->user_id,&now,date,out); }
    storage_free_reservations(&list); return result;
}

static BookingResult batch_base(const BookingContext *context, const AuthSession *session,
                                 int date, int start, int duration, int no, LabTime *now, Lab computers[COMPUTER_COUNT])
{
    BookingResult result;
    int slot;
    if(!storage_writes_allowed()) { return BOOKING_RECOVERY_REQUIRED; }
    result=require_student(session);
    if(result!=BOOKING_OK) { return result; }
    if(!read_now(context,now)) { return BOOKING_CLOCK_ERROR; }
    for(slot=start;slot<start+duration;++slot) {
        result=check_time_at(now,date,slot,1);
        if(result!=BOOKING_OK) { return result; }
    }
    if(storage_load_computers(computers)!=1) { return BOOKING_STORAGE_ERROR; }
    if(computers[no-1].status==COMPUTER_FAULT) { return BOOKING_COMPUTER_FAULT; }
    return BOOKING_OK;
}
static BookingResult batch_snapshot(const AuthSession *session, int date, int start, int duration, int no,
                                     const ReservationList *list, const LabTime *now, const Lab computers[COMPUTER_COUNT])
{
    BookingAvailability view;
    unsigned int request=booking_range_mask(start,duration);
    BookingResult result=availability_at(list,computers,session->user_id,now,date,&view);
    if(result!=BOOKING_OK) { return result; }
    if(view.own_conflicts & request) { return BOOKING_STUDENT_CONFLICT; }
    return (view.masks[no-1] & request)==request ? BOOKING_OK : BOOKING_SEAT_OCCUPIED;
}
static BookingResult prepare_batch(const BookingContext *context, const AuthSession *session,
                                    int date, int start, int duration, int no, ReservationList *list, int *maximum)
{
    BookingResult result;
    LabTime now;
    Lab computers[COMPUTER_COUNT];
    size_t i;
    if(!auth_session_has_role(session,ROLE_STUDENT)) { return BOOKING_UNAUTHORIZED; }
    if(!booking_range_mask(start,duration) || !utils_valid_date(date) || no<1 || no>COMPUTER_COUNT) { return BOOKING_INVALID_INPUT; }
    result=batch_base(context,session,date,start,duration,no,&now,computers);
    if(result!=BOOKING_OK) { return result; }
    /* 唯一一次预约加载。后续在同一进程操作的已验证源快照上复查。 */
    result=from_storage(storage_load_reservations(list));
    if(result!=BOOKING_OK) { return result; }
    result=batch_snapshot(session,date,start,duration,no,list,&now,computers);
    if(result!=BOOKING_OK) { return result; }
    if(list->count>(size_t)(MAX_RESERVATIONS-duration)) { return BOOKING_CAPACITY; }
    *maximum=0;
    for(i=0;i<list->count;++i) { if(list->items[i].id>*maximum) { *maximum=list->items[i].id; } }
    return *maximum>INT_MAX-duration ? BOOKING_ID_EXHAUSTED : BOOKING_OK;
}
BookingResult booking_check_batch(const BookingContext *context, const AuthSession *session,
                                  int date, int start, int duration, int no)
{
    ReservationList list={0}; int maximum;
    BookingResult result=prepare_batch(context,session,date,start,duration,no,&list,&maximum);
    storage_free_reservations(&list); return result;
}
BookingResult booking_create_batch(const BookingContext *context, const AuthSession *session,
                                   int date, int start, int duration, int no, BatchBooking *out)
{
    ReservationList list={0}, source;
    BatchBooking result={0};
    BookingResult status;
    StorageResult saved;
    LabTime now;
    Lab computers[COMPUTER_COUNT];
    int maximum, i;
    size_t original;
    if(!out) { return BOOKING_INVALID_INPUT; }
    status=prepare_batch(context,session,date,start,duration,no,&list,&maximum);
    if(status!=BOOKING_OK) { goto done; }
    original=list.count;
    status=from_storage(storage_reserve_reservations(&list,list.count+(size_t)duration));
    if(status!=BOOKING_OK) { goto done; }
    for(i=0;i<duration;++i) {
        Reservation record={0};
        record.id=maximum+i+1; strcpy(record.student_id,session->user_id);
        record.date=date; record.slot_index=start+i; record.computer_no=no; record.status=BOOKING_ACTIVE;
        list.items[list.count++]=record; result.records[i]=record;
    }
    /* 借用原始前缀，不释放source；包含新记录的候选不能拿来与自身做冲突判断。 */
    source=list; source.count=original;
    status=batch_base(context,session,date,start,duration,no,&now,computers);
    if(status==BOOKING_OK) { status=batch_snapshot(session,date,start,duration,no,&source,&now,computers); }
    if(status==BOOKING_OK) {
        saved=storage_save_reservations(&list); /* 唯一提交边界，完整快照替换。 */
        status=saved==STORAGE_RECOVERY_REQUIRED ? BOOKING_RECOVERY_REQUIRED : from_storage(saved);
    }
    if(status==BOOKING_OK) { result.count=duration; *out=result; }
done:
    storage_free_reservations(&list); return status;
}
