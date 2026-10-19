#ifndef SMART_LAB_BOOKING_H
#define SMART_LAB_BOOKING_H

#include "auth.h"
#include "utils.h"

/* 业务失败均不写入有效预约，也不发布成功输出。 */
typedef enum {
    BOOKING_OK = 0, BOOKING_INVALID_INPUT, BOOKING_UNAUTHORIZED,
    BOOKING_STUDENT_NOT_FOUND, BOOKING_COMPUTER_FAULT, BOOKING_SEAT_OCCUPIED,
    BOOKING_STUDENT_CONFLICT, BOOKING_NOT_FOUND, BOOKING_NOT_OWNER,
    BOOKING_ALREADY_CANCELLED, BOOKING_TIME_EXPIRED, BOOKING_WINDOW_EXCEEDED,
    BOOKING_STORAGE_ERROR, BOOKING_CAPACITY, BOOKING_NO_MEMORY,
    BOOKING_ID_EXHAUSTED, BOOKING_CLOCK_ERROR, BOOKING_RECOVERY_REQUIRED
} BookingResult;

/* 时间依赖由调用方显式传入；NULL 上下文/函数使用本地系统时间，生产无硬编码时间。 */
typedef struct {
    int (*now)(void *context, LabTime *time); /* 成功 1，失败 0；测试可注入固定时钟。 */
    void *clock_context;                   /* 时钟自有上下文，不被预约模块释放。 */
} BookingContext;

/* 预约查询视图。 */
typedef enum {
    BOOKING_QUERY_ACTIVE = 0, /* 所有有效记录，含已过去但未取消记录。 */
    BOOKING_QUERY_ALL = 1,    /* 本人全部历史及取消记录。 */
    BOOKING_QUERY_CANCELLABLE = 2 /* 本人有效且尚未开始，供取消入口。 */
} BookingQuery;

/* 指定日期/时段的只读座位结果，不暴露其他学生标识。 */
typedef struct {
    char symbols[COMPUTER_COUNT]; /* 按编号，故障 ! 优先于预约 #，否则 *。 */
    int free_count;               /* 当前视图空闲座位数。 */
    int booked_count;             /* 当前视图非故障且预约的座位数。 */
    int fault_count;              /* 当前故障座位数。 */
} BookingSeats;

/* 功能：检查日期、未开始及可选七日窗口。参数：context 时钟；date/slot 目标；window 非零限窗口。
 * 返回：BookingResult。已到开始时刻（含相等）即拒绝。
 */
BookingResult booking_check_time(const BookingContext *context, int date, int slot, int window);

/* 功能：检测两类冲突；参数：list 为已经验证的集合；student_id 学号；date/slot/no 目标。
 * 返回：BookingResult；只查有效记录，不修改数据。
 */
BookingResult booking_check_conflict(const ReservationList *list, const char *student_id,
                                    int date, int slot, int no);

/* 功能：完整预检创建，供 UI 确认前调用。参数：context/session/date/slot/no 目标。
 * 返回：BookingResult；不保存，也不分配正式编号。
 */
BookingResult booking_check_create(const BookingContext *context, const AuthSession *session,
                                  int date, int slot, int no);

/* 功能：从 session 取得学号，重新验证当前文件/时间后创建并保存。
 * 参数：context 时钟；session 学生会话；date/slot/no 目标；created 成功输出。
 * 返回：BookingResult；失败不修改输出、账户或原预约文件。
 */
BookingResult booking_create(const BookingContext *context, const AuthSession *session,
                            int date, int slot, int no, Reservation *created);

/* 功能：完整预检取消并取得可确认详情。参数：context/session/id 输入；record 成功输出。
 * 返回：BookingResult；不写文件。
 */
BookingResult booking_check_cancel(const BookingContext *context, const AuthSession *session,
                                  int id, Reservation *record);

/* 功能：重新校验本人、有效且未开始后标记取消并保存。
 * 参数：context/session/id 输入。返回：BookingResult；写失败原预约仍有效。
 */
BookingResult booking_cancel(const BookingContext *context, const AuthSession *session, int id);

/* 功能：只返回当前学生记录，按日期/时段/编号排序，不改文件。
 * 参数：context/session/mode 输入；result 为零初始化/有效自有集合，成功替换。
 * 返回：BookingResult；失败保持原输出，调用方最终 storage_free_reservations。
 */
BookingResult booking_query(const BookingContext *context, const AuthSession *session,
                           BookingQuery mode, ReservationList *result);

/* 功能：重新读取电脑与预约计算公共占用，不展示身份信息，允许查看历史有效日期。
 * 参数：date/slot 目标；seats 成功输出。返回：BookingResult；失败不改输出。
 */
BookingResult booking_get_seats(int date, int slot, BookingSeats *seats);

/* 功能：结果提示。参数：result。返回：无密码的静态说明文本。 */
const char *booking_result_message(BookingResult result);

/* 只读当日可用性。低九位含设备、座位、本人冲突、窗口及开始时刻限制。
 * 成功才写out；一次完整加载，不暴露其他学生身份。 */
typedef struct {
    unsigned int masks[COMPUTER_COUNT];
    int loads[COMPUTER_COUNT];
    unsigned int own_conflicts;
    LabTime as_of;
} BookingAvailability;
unsigned int booking_range_mask(int start, int duration);
BookingResult booking_availability(const BookingContext *context, const AuthSession *session,
                                   int date, BookingAvailability *out);

#endif
