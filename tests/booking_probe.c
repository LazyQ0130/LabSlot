/* P2 业务/存储/内存故障测试驱动，不输出密码，不编入正式程序。 */
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "storage.h"
#include "booking.h"
#include "test_clock.h"

/* 此文件实现测试替身，需要调用原标准函数，其他翻译单元仍由预包含头重定向。 */
#undef fprintf
#undef fflush
#undef fclose

static int fail_at; /* 第几次动态分配失败，0 不失败；只存在于测试驱动。 */
static int allocations; /* 被测操作的实际动态分配次数。 */
static int tracking; /* 登录准备结束后才开始计数。 */
static const char *io_failure; /* 仅测试驱动注入 fprintf/fflush/fclose 失败。 */
static int io_failed; /* 只失败一次，允许错误报告正常输出。 */
static FILE *writing; /* 仅跟踪被测文件写流，不影响输入或 stdout/stderr。 */

void *__real_malloc(size_t size);
void *__real_calloc(size_t count, size_t size);
void *__real_realloc(void *pointer, size_t size);

/* 功能：确定是否注入一次失败。参数：无。返回：应失败 1，否则 0。 */
static int allocation_fails(void)
{
    if (!tracking) { return 0; }
    ++allocations;
    return fail_at > 0 && allocations == fail_at;
}

/* 功能：链接器 malloc 包装。参数/返回同标准 malloc。 */
void *__wrap_malloc(size_t size) { return allocation_fails() ? NULL : __real_malloc(size); }
/* 功能：链接器 calloc 包装。参数/返回同标准 calloc。 */
void *__wrap_calloc(size_t count, size_t size) { return allocation_fails() ? NULL : __real_calloc(count, size); }
/* 功能：链接器 realloc 包装。参数/返回同标准 realloc。 */
void *__wrap_realloc(void *pointer, size_t size) { return allocation_fails() ? NULL : __real_realloc(pointer, size); }

/* 功能：测试写失败。参数、返回同 fprintf；模拟 ENOSPC，不输出数据到日志。 */
int lab_test_fprintf(FILE *stream, const char *format, ...)
{
    int result; /* 实际格式化结果。 */
    va_list args; /* 可变参数。 */
    if (tracking && stream != stdout && stream != stderr) {
        writing = stream;
        if (!io_failed && io_failure != NULL && strcmp(io_failure, "write") == 0) {
            io_failed = 1; errno = ENOSPC; return -1;
        }
    }
    va_start(args, format); result = vfprintf(stream, format, args); va_end(args);
    return result;
}

/* 功能：测试刷新失败。参数、返回同 fflush；只作用于正在保存的文件。 */
int lab_test_fflush(FILE *stream)
{
    if (!io_failed && writing == stream && io_failure != NULL && strcmp(io_failure, "flush") == 0) {
        io_failed = 1; errno = EIO; return EOF;
    }
    return fflush(stream);
}

/* 功能：测试关闭失败，仍实际关闭底层资源。参数、返回同 fclose。 */
int lab_test_fclose(FILE *stream)
{
    int injected = !io_failed && writing == stream && io_failure != NULL && strcmp(io_failure, "close") == 0;
    int result = fclose(stream); /* 不为故障模拟泄漏流。 */
    if (injected) { io_failed = 1; errno = EIO; return EOF; }
    return result;
}

#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "Probe check failed at %d\n", __LINE__); return 9; } } while (0)

/* 功能：解析 int 测试参数，包括负数。参数：text。返回：int，非法时返回 INT_MIN。 */
static int integer(const char *text)
{
    char *end; /* 未消费位置。 */
    long value; /* 先检查范围再缩窄。 */
    errno = 0; value = strtol(text, &end, 10);
    return errno || *end != '\0' || end == text || value < INT_MIN || value > INT_MAX ? INT_MIN : (int)value;
}

/* 功能：执行单个公开 API。参数：命令/会话/时钟/三个数值输入。返回：业务结果。
 * 输出只含预约事实；失败时同时断言输出缓冲区未被部分修改。
 */
static BookingResult operation(const char *mode, const AuthSession *session, const BookingContext *context,
                                int a, int b, int c)
{
    Reservation record, before; /* 失败输出的字节哨兵。 */
    ReservationList list = {0}; /* 查询输出。 */
    BookingSeats seats, old_seats; /* 座位输出哨兵。 */
    BookingResult result = BOOKING_INVALID_INPUT; /* 默认结果。 */
    size_t i; /* 输出结果行。 */
    memset(&record, 0x5a, sizeof record); before = record;
    memset(&seats, 0x5a, sizeof seats); old_seats = seats;
    if (strcmp(mode, "create") == 0) { result = booking_create(context, session, a, b, c, &record); }
    else if (strcmp(mode, "check-create") == 0) { result = booking_check_create(context, session, a, b, c); }
    else if (strcmp(mode, "cancel") == 0) { result = booking_cancel(context, session, a); }
    else if (strcmp(mode, "check-cancel") == 0) { result = booking_check_cancel(context, session, a, &record); }
    else if (strcmp(mode, "time") == 0) { result = booking_check_time(context, a, b, c); }
    else if (strcmp(mode, "seats") == 0) {
        result = booking_get_seats(a, b, &seats);
        if (result == BOOKING_OK) {
            printf("SEATS="); for (i = 0; i < COMPUTER_COUNT; ++i) { putchar(seats.symbols[i]); }
            printf("\nCOUNTS=%d,%d,%d\n", seats.free_count, seats.booked_count, seats.fault_count);
        }
    } else if (strcmp(mode, "query") == 0) {
        result = booking_query(context, session, (BookingQuery)a, &list);
        if (result == BOOKING_OK) {
            printf("COUNT=%zu\n", list.count);
            for (i = 0; i < list.count; ++i) {
                const Reservation *r = &list.items[i]; /* 仅本人的返回记录。 */
                printf("ROW=%d,%s,%d,%d,%d,%d\n", r->id, r->student_id, r->date, r->slot_index, r->computer_no, (int)r->status);
            }
        }
        if (result != BOOKING_OK && (list.items != NULL || list.count != 0 || list.capacity != 0)) { exit(9); }
        storage_free_reservations(&list);
    }
    if (result != BOOKING_OK &&
        (memcmp(&record, &before, sizeof record) || memcmp(&seats, &old_seats, sizeof seats))) { exit(9); }
    if (result == BOOKING_OK && (strcmp(mode, "create") == 0 || strcmp(mode, "check-cancel") == 0)) {
        printf("ID=%d\n", record.id);
    }
    return result;
}

/* 功能：测试入口。参数：mode who a b c；环境仅供测试时钟/分配失败。返回：驱动检查状态。 */
int main(int argc, char **argv)
{
    AuthSession session = {0}; /* 正常模式使用真实账户登录取得会话。 */
    TestClock clock; /* 固定时间。 */
    BookingContext context = {test_clock_now, &clock}; /* 不涉及生产全局变量。 */
    const char *failure = getenv("LAB_TEST_FAIL_ALLOC"); /* 分配故障序号。 */
    BookingResult result; /* 被测 API 结果。 */
    int a, b, c; /* 可选整数参数。 */
    CHECK(argc >= 3);
    test_clock_init(&clock);
    if (strcmp(argv[2], "none") == 0) { /* 保持空会话。 */ }
    else if (strcmp(argv[2], "ghost") == 0) { session.role = ROLE_STUDENT; session.logged_in = 1; strcpy(session.user_id, "Missing"); }
    else if (strcmp(argv[2], "admin") == 0) { CHECK(auth_login_admin("Admin", "P2Synthetic!", &session) == AUTH_OK); }
    else if (strcmp(argv[2], "forged") == 0) { session.role = ROLE_STUDENT; session.logged_in = 1; strcpy(session.user_id, "U1"); }
    else { CHECK(auth_login_student(argv[2], "P2Synthetic!", &session) == AUTH_OK); }
    a = argc > 3 ? integer(argv[3]) : 0;
    b = argc > 4 ? integer(argv[4]) : 0;
    c = argc > 5 ? integer(argv[5]) : 0;
    tracking = 1; fail_at = failure == NULL ? 0 : integer(failure);
    io_failure = getenv("LAB_TEST_IO_FAILURE");
    if (strcmp(argv[1], "load") == 0) {
        ReservationList list = {0}; /* 输出哨兵，失败时原快照必须保留。 */
        Reservation *old; /* 原指针，失败不能被 realloc 半成品替换。 */
        StorageResult status; /* 细分存储结果。 */
        tracking = 0;
        CHECK(storage_reserve_reservations(&list, 1) == STORAGE_OK);
        list.items[0] = (Reservation){777,"U1",1,20000101,0,BOOKING_CANCELLED}; list.count = 1;
        old = list.items; tracking = 1;
        status = storage_load_reservations(&list);
        if (status != STORAGE_OK) { CHECK(list.items == old && list.count == 1 && list.items[0].id == 777); }
        printf("STORAGE=%d\nCOUNT=%zu\n", (int)status, list.count);
        storage_free_reservations(&list);
    } else if (strcmp(argv[1], "reserve") == 0) {
        ReservationList list = {0}; /* 先分配旧数组，然后强制扩容失败。 */
        Reservation *old; /* 原数组。 */
        tracking = 0; CHECK(storage_reserve_reservations(&list, 1) == STORAGE_OK);
        old = list.items; fail_at = 1; allocations = 0; tracking = 1;
        CHECK(storage_reserve_reservations(&list, 65) == STORAGE_NO_MEMORY);
        CHECK(list.items == old && list.capacity == 64 && list.count == 0);
        fail_at = 0;
        CHECK(storage_reserve_reservations(&list, 65) == STORAGE_OK);
        CHECK(storage_reserve_reservations(&list, MAX_RESERVATIONS + 1) == STORAGE_CAPACITY);
        storage_free_reservations(&list); CHECK(list.items == NULL && list.count == 0 && list.capacity == 0);
        puts("RESERVE_OK");
    } else if (strcmp(argv[1], "calendar") == 0) {
        char period[16]; /* 时段格式输出。 */
        printf("VALID=%d\nSERIAL=%d\nNEXT=%d\n", utils_valid_date(a), utils_date_serial(a), utils_date_add(a, b));
        CHECK(utils_format_slot(c, period, sizeof period)); puts(period);
        CHECK(!utils_format_slot(c, period, 1));
    } else if (strcmp(argv[1], "validate") == 0) {
        ReservationList list = {0}; /* 两条完整快照的故意损坏测试。 */
        StorageResult status; /* 保存前验证结果。 */
        CHECK(storage_load_reservations(&list) == STORAGE_OK && list.count >= 2);
        switch (a) {
        case 1: list.items[0].id = 0; break;
        case 2: list.items[1].id = list.items[0].id; break;
        case 3: memset(list.items[0].student_id, 'X', ID_LEN); break;
        case 4: list.items[0].computer_no = 41; break;
        case 5: list.items[0].date = 20260229; break;
        case 6: list.items[0].slot_index = 9; break;
        case 7: list.items[0].status = (BookingStatus)2; break;
        case 8: case 9:
            list.items[0].status = BOOKING_ACTIVE;
            list.items[1] = list.items[0]; list.items[1].id += 1;
            if (a == 8) { strcpy(list.items[1].student_id, "U2"); }
            else { list.items[1].computer_no = list.items[0].computer_no == 40 ? 1 : list.items[0].computer_no + 1; }
            break;
        case 10: list.count = MAX_RESERVATIONS + 1; list.capacity = MAX_RESERVATIONS + 1; break;
        default: CHECK(0);
        }
        status = storage_save_reservations(&list);
        printf("STORAGE=%d\n", (int)status);
        storage_free_reservations(&list);
    } else {
        result = operation(argv[1], &session, &context, a, b, c);
        printf("RESULT=%d\nALLOCATIONS=%d\n", (int)result, allocations);
        if (getenv("LAB_TEST_RECOVER") != NULL) {
            CHECK(result != BOOKING_OK); fail_at = 0;
            result = operation(argv[1], &session, &context, a, b, c);
            printf("RECOVERY=%d\n", (int)result);
        }
    }
    auth_logout(&session);
    return 0;
}
