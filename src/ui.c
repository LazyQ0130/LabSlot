#include "ui.h"

#include <stdio.h>
#include <string.h>
#include <limits.h>
#include "auth.h"
#include "lab.h"
#include "utils.h"
#include "storage.h"
#include "fault.h"
#include "admin.h"

#ifdef _WIN32
#include <windows.h>
#endif

/* 功能：只调整实际 Windows 控制台的代码页；参数：无；返回：无。 */
void ui_initialize_console(void)
{
#ifdef _WIN32
    DWORD mode; /* 判断标准句柄是否连接控制台，而非重定向文件/管道。 */
    if (GetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), &mode) &&
        !SetConsoleOutputCP(CP_UTF8)) {
        fputs("警告：设置控制台 UTF-8 输出失败。\n", stderr);
    }
    if (GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), &mode) &&
        !SetConsoleCP(CP_UTF8)) {
        fputs("警告：设置控制台 UTF-8 输入失败。\n", stderr);
    }
#endif
}

/* 功能：使用等宽 ASCII 单元格打印座位；参数及返回值见 ui.h。 */
void ui_print_seat_map(const Lab computers[COMPUTER_COUNT])
{
    int row; /* 座位图行号。 */
    int col; /* 座位图列号。 */
    puts("\n实验室座位图（5 排 × 8 列）");
    puts("* 正常/基础空闲    ! 故障（指定日期/时段预约状态请在学生菜单查看）");
    for (row = 0; row < LAB_ROWS; ++row) {
        for (col = 0; col < LAB_COLUMNS; ++col) {
            const Lab *seat = &computers[row * LAB_COLUMNS + col]; /* 当前座位。 */
            printf("%02d[%c]%s", seat->no, lab_seat_symbol(seat),
                   col + 1 == LAB_COLUMNS ? "\n" : " ");
        }
    }
    puts("");
}

/* 功能：读取菜单并重试非法行。参数：maximum 上界；choice 输出。返回：InputResult。 */
static InputResult read_menu(int maximum, int *choice)
{
    for (;;) {
        InputResult result = utils_read_int_range("请输入选项：", 0, maximum, choice); /* 当前输入。 */
        if (result != INPUT_INVALID) {
            return result;
        }
        printf("输入无效，请输入 0～%d 的整数（单行不超过 127 字节）。\n", maximum);
    }
}

/* 功能：收集注册字段和密码确认，失败不创建账户。
 * 参数：role 目标角色。返回：INPUT_OK/INVALID 回入口，EOF/ERROR 传播至主菜单。
 */
static InputResult register_user(UserRole role)
{
    Student draft = {0}; /* 复用相同字段布局的学生候选，time 默认 0。 */
    Admin admin = {0}; /* 管理员注册时显式复制字段，不进行结构体强转。 */
    char confirmation[PASSWORD_LEN] = {0}; /* 二次确认，仅短暂存在。 */
    InputResult input; /* 任意字段失败立即取消，不使用截断内容。 */
    AuthResult result; /* 认证模块结果，只有 AUTH_OK 才报成功。 */
    input = utils_read_field(role == ROLE_ADMIN ? "工号：" : "学号：", draft.id, sizeof draft.id, 0);
    if (input != INPUT_OK) {
        goto done;
    }
    if (!utils_valid_id(draft.id)) {
        input = INPUT_INVALID;
        goto done;
    }
    input = utils_read_field("姓名：", draft.name, sizeof draft.name, 0);
    if (input != INPUT_OK) {
        goto done;
    }
    input = utils_read_field("密码：", draft.password, sizeof draft.password, 1);
    if (input != INPUT_OK) {
        goto done;
    }
    input = utils_read_field("再次输入密码：", confirmation, sizeof confirmation, 1);
    if (input != INPUT_OK) {
        goto done;
    }
    if (strcmp(draft.password, confirmation) != 0) {
        puts("两次密码不一致，未创建账户。");
        goto done;
    }
    if (role == ROLE_ADMIN) {
        strcpy(admin.id, draft.id);
        strcpy(admin.name, draft.name);
        strcpy(admin.password, draft.password);
        result = auth_register_admin(&admin);
    } else {
        result = auth_register_student(&draft);
    }
    if (result == AUTH_OK) {
        puts("注册成功，请选择登录。");
    } else {
        printf("注册失败：%s。\n", auth_result_message(result));
    }
done:
    if (input == INPUT_INVALID) {
        puts("输入无效：字段不能为空、超长或含非法字符；本次注册已取消。");
    }
    utils_clear_secret(&draft, sizeof draft);
    utils_clear_secret(&admin, sizeof admin);
    utils_clear_secret(confirmation, sizeof confirmation);
    return input;
}

/* 功能：读取真实日期和时段。参数：date/slot 输出。返回：InputResult。 */
static InputResult read_booking_target(int *date, int *slot)
{
    char text[10]; /* 八位日期、结尾及一位超长检测空间。 */
    InputResult input = utils_read_field("预约日期（YYYYMMDD）：", text, sizeof text, 0); /* 日期行。 */
    int i; /* 九个时段。 */
    if (input != INPUT_OK) { return input; }
    if (strlen(text) != 8 || !utils_parse_int(text, 10000101, 99991231, date) || !utils_valid_date(*date)) {
        return INPUT_INVALID;
    }
    for (i = 0; i < SLOT_COUNT; ++i) {
        char period[16]; /* 时段说明。 */
        utils_format_slot(i, period, sizeof period);
        printf("%d. %s\n", i, period);
    }
    return utils_read_int_range("时段索引（0～8）：", 0, SLOT_COUNT - 1, slot);
}

/* 功能：绘制指定日期/时段真实视图。参数：date/slot/view 成功业务结果。返回：无。 */
static void print_booking_seats(int date, int slot, const BookingSeats *view)
{
    char period[16]; /* 格式化时段。 */
    int i; /* 电脑数组下标。 */
    utils_format_slot(slot, period, sizeof period);
    printf("\n实验室座位图\n日期：%04d-%02d-%02d\n时间：%s\n", date / 10000, date / 100 % 100, date % 100, period);
    puts("* 空闲    # 已预约    ! 故障（故障优先）");
    for (i = 0; i < COMPUTER_COUNT; ++i) {
        printf("%02d[%c]%s", i + 1, view->symbols[i], (i + 1) % LAB_COLUMNS == 0 ? "\n" : " ");
    }
    printf("座位符号数量：空闲 %d，已预约 %d，故障 %d。\n", view->free_count, view->booked_count, view->fault_count);
}

/* 功能：打印一条已验证记录。参数：record。返回：无。 */
static void print_reservation(const Reservation *record)
{
    char period[16]; /* 时段起止时间。 */
    utils_format_slot(record->slot_index, period, sizeof period);
    printf("预约 %d | 日期 %08d | %s | 电脑 %02d | %s\n", record->id, record->date, period,
           record->computer_no, record->status == BOOKING_ACTIVE ? "有效" : "已取消");
}

/* 功能：打印本人结果。参数：list 成功查询集合。返回：无。 */
static void print_reservations(const ReservationList *list)
{
    size_t i; /* 结果下标。 */
    if (list->count == 0) { puts("暂无符合条件的预约。"); }
    for (i = 0; i < list->count; ++i) { print_reservation(&list->items[i]); }
}

/* 功能：学生查看视图或确认后预约。参数：context/session；create 非零为预约。
 * 返回：InputResult；业务失败回菜单，EOF/ERROR 传播。
 */
static InputResult seat_booking_flow(const BookingContext *context, const AuthSession *session, int create)
{
    int date, slot, no, confirm; /* 完整读取后才使用的目标与确认。 */
    BookingSeats view; /* 成功后才显示的真实座位。 */
    Reservation created; /* 保存成功后才发布的预约详情。 */
    BookingResult result; /* 业务结果。 */
    InputResult input = read_booking_target(&date, &slot); /* 有界输入状态。 */
    if (input != INPUT_OK) { return input; }
    result = create ? booking_check_time(context, date, slot, 1) : BOOKING_OK;
    if (result == BOOKING_OK) { result = booking_get_seats(date, slot, &view); }
    if (result != BOOKING_OK) { printf("操作失败：%s。\n", booking_result_message(result)); return INPUT_OK; }
    print_booking_seats(date, slot, &view);
    if (!create) { return INPUT_OK; }
    input = utils_read_int_range("电脑编号（1～40）：", 1, COMPUTER_COUNT, &no);
    if (input != INPUT_OK) { return input; }
    result = booking_check_create(context, session, date, slot, no);
    if (result != BOOKING_OK) { printf("预约失败：%s。\n", booking_result_message(result)); return INPUT_OK; }
    printf("确认预约：学号 %s，日期 %08d，时段 %02d:00-%02d:00，电脑 %02d。\n",
           session->user_id, date, OPEN_HOUR + slot, OPEN_HOUR + slot + 1, no);
    puts("1. 确认预约\n0. 放弃");
    input = read_menu(1, &confirm);
    if (input != INPUT_OK) { return input; }
    if (!confirm) { puts("已放弃预约，未创建记录。"); return INPUT_OK; }
    result = booking_create(context, session, date, slot, no, &created);
    if (result == BOOKING_OK) { puts("预约成功。"); print_reservation(&created); }
    else { printf("预约失败：%s。\n", booking_result_message(result)); }
    return INPUT_OK;
}

/* 功能：默认有效预约，可选择全部历史。参数：context/session。返回：InputResult。 */
static InputResult query_booking_flow(const BookingContext *context, const AuthSession *session)
{
    ReservationList list = {0}; /* 堆上个人结果。 */
    BookingResult result = booking_query(context, session, BOOKING_QUERY_ACTIVE, &list);
    InputResult input = INPUT_OK; /* 查询失败不假装暂无记录。 */
    int choice; /* 0 返回，1 全部历史。 */
    if (result != BOOKING_OK) { printf("查询失败：%s。\n", booking_result_message(result)); goto done; }
    puts("我的有效预约（含过去未取消记录）："); print_reservations(&list);
    puts("1. 查看全部历史与已取消记录\n0. 返回学生菜单");
    input = read_menu(1, &choice);
    if (input != INPUT_OK || choice == 0) { goto done; }
    result = booking_query(context, session, BOOKING_QUERY_ALL, &list);
    if (result == BOOKING_OK) { puts("我的全部预约："); print_reservations(&list); }
    else { printf("查询失败：%s。\n", booking_result_message(result)); }
done:
    storage_free_reservations(&list);
    return input;
}

/* 功能：展示可取消记录，校验详情并确认后取消。参数：context/session。返回：InputResult。 */
static InputResult cancel_booking_flow(const BookingContext *context, const AuthSession *session)
{
    ReservationList list = {0}; /* 可取消结果。 */
    Reservation candidate; /* 通过预检的详情。 */
    BookingResult result = booking_query(context, session, BOOKING_QUERY_CANCELLABLE, &list);
    InputResult input = INPUT_OK; /* EOF/错误释放集合后传播。 */
    int id, confirm; /* 待取消编号与确认。 */
    if (result != BOOKING_OK) { printf("取消查询失败：%s。\n", booking_result_message(result)); goto done; }
    puts("本人尚未开始的可取消预约："); print_reservations(&list);
    if (list.count == 0) { goto done; }
    input = utils_read_int_range("预约编号：", 1, INT_MAX, &id);
    if (input != INPUT_OK) { goto done; }
    result = booking_check_cancel(context, session, id, &candidate);
    if (result != BOOKING_OK) { printf("取消失败：%s。\n", booking_result_message(result)); goto done; }
    print_reservation(&candidate);
    puts("1. 确认取消\n0. 保留预约");
    input = read_menu(1, &confirm);
    if (input != INPUT_OK) { goto done; }
    if (!confirm) { puts("已放弃取消，原预约不变。"); goto done; }
    result = booking_cancel(context, session, id);
    if (result == BOOKING_OK) { puts("取消成功，该时段座位已经释放。"); }
    else { printf("取消失败：%s。\n", booking_result_message(result)); }
done:
    storage_free_reservations(&list);
    return input;
}


/* 功能：从存储刷新公共基础座位，防止维护后继续显示启动快照。参数：无。返回：无。 */
static void show_current_computers(void)
{
    Lab current[COMPUTER_COUNT];
    if (storage_writes_allowed() && storage_load_computers(current) == 1) { ui_print_seat_map(current); }
    else { puts("座位读取失败，请检查数据或重启恢复维护。"); }
}

/* 功能：打印无凭据报告。参数：report。返回：无。 */
static void print_report(const FaultReport *report)
{
    printf("报告 %d | 学号 %s | 电脑 %02d | 日期 %08d | %s\n说明：%s\n", report->id,
           report->student_id, report->computer_no, report->date,
           report->status == REPORT_PENDING ? "待处理" : "已处理", report->detail);
}

/* 功能：按权限显示报告。参数：会话/电脑0全部/状态-1全部。返回：无。 */
static void show_reports(const AuthSession *session, int no, int status)
{
    FaultReportList reports = {0};
    FaultResult result = fault_query_reports(session, no, status, &reports);
    size_t i;
    if (result != FAULT_OK) { printf("查询失败：%s。\n", fault_result_message(result)); }
    else {
        if (reports.count == 0) { puts("暂无符合条件的故障报告。"); }
        for (i = 0; i < reports.count; ++i) { print_report(&reports.items[i]); }
    }
    storage_free_reports(&reports);
}

/* 功能：学生查看本机历史并确认上报，身份来自会话。参数：时钟/会话。返回：InputResult。 */
static InputResult submit_fault_flow(const BookingContext *context, const AuthSession *session)
{
    Lab computers[COMPUTER_COUNT];
    FaultReport created;
    char detail[DETAIL_LEN];
    int no, confirm;
    FaultResult result;
    InputResult input = utils_read_int_range("电脑编号（1～40）：", 1, COMPUTER_COUNT, &no);
    if (input != INPUT_OK) { return input; }
    result = fault_read_computers(session, computers);
    if (result != FAULT_OK) { printf("读取失败：%s。\n", fault_result_message(result)); return INPUT_OK; }
    printf("电脑 %02d 当前状态：%s。\n", no, computers[no-1].status == COMPUTER_FAULT ? "故障" : "正常");
    puts("本人在该电脑的历史报告："); show_reports(session, no, -1);
    if (computers[no-1].status == COMPUTER_FAULT) { puts("电脑已有故障，无需重复提交。"); return INPUT_OK; }
    input = utils_read_field("故障说明（UTF-8 最多255字节）：", detail, sizeof detail, 0);
    if (input != INPUT_OK) { return input; }
    printf("报告人：%s；电脑：%02d；说明：%s\n", session->user_id, no, detail);
    puts("1. 确认上报\n0. 放弃");
    input = read_menu(1, &confirm);
    if (input != INPUT_OK || !confirm) { return input; }
    result = fault_submit(context, session, no, detail, &created);
    if (result == FAULT_OK) { printf("上报成功，报告编号：%d。\n", created.id); }
    else { printf("上报失败：%s。\n", fault_result_message(result)); }
    return INPUT_OK;
}

/* 功能：展示确认前受影响预约及学号。参数：affected。返回：无。 */
static void print_affected(const ReservationList *affected)
{
    size_t i;
    printf("确认后将取消 %zu 条尚未开始的有效预约：\n", affected->count);
    for (i = 0; i < affected->count; ++i) {
        print_reservation(&affected->items[i]);
        printf("学号：%s\n", affected->items[i].student_id);
    }
}

/* 功能：管理员预览并确认电脑维护。参数：时钟/会话/目标状态。返回：InputResult。 */
static InputResult change_computer_flow(const BookingContext *context, const AuthSession *session, ComputerStatus target)
{
    ReservationList affected = {0};
    int no, confirm;
    FaultResult result;
    InputResult input = utils_read_int_range("电脑编号（1～40）：", 1, COMPUTER_COUNT, &no);
    if (input != INPUT_OK) { return input; }
    result = fault_preview_change(context, session, no, target, &affected);
    if (result != FAULT_OK) { printf("维护未执行：%s。\n", fault_result_message(result)); goto done; }
    printf("电脑 %02d 将%s。\n", no, target == COMPUTER_FAULT ? "标记故障" : "恢复正常（不恢复已取消预约）");
    print_affected(&affected);
    puts("1. 确认维护\n0. 放弃");
    input = read_menu(1, &confirm);
    if (input != INPUT_OK || !confirm) { goto done; }
    result = fault_change_computer(context, session, no, target);
    printf("维护%s：%s。\n", result == FAULT_OK ? "成功" : "未确认成功", fault_result_message(result));
done:
    storage_free_reservations(&affected); return input;
}

/* 功能：管理员处理报告，报告更新晚于关联维护。参数：时钟/会话。返回：InputResult。 */
static InputResult process_report_flow(const BookingContext *context, const AuthSession *session)
{
    FaultReport report;
    ReservationList affected = {0};
    int id, action, confirm;
    FaultResult result;
    InputResult input = utils_read_int_range("报告编号：", 1, INT_MAX, &id);
    if (input != INPUT_OK) { return input; }
    result = fault_get_report(session, id, &report);
    if (result != FAULT_OK) { printf("查询失败：%s。\n", fault_result_message(result)); return INPUT_OK; }
    print_report(&report);
    puts("1. 确认故障并标记电脑故障\n2. 电脑已维修，恢复正常后关闭报告\n3. 未发现故障，仅关闭报告\n0. 返回");
    input = read_menu(3, &action);
    if (input != INPUT_OK || action == 0) { return input; }
    result = fault_preview_process(context, session, id, (FaultAction)action, &report, &affected);
    if (result != FAULT_OK) { printf("处理未执行：%s。\n", fault_result_message(result)); goto done; }
    print_affected(&affected);
    puts("1. 确认处理\n0. 放弃");
    input = read_menu(1, &confirm);
    if (input != INPUT_OK || !confirm) { goto done; }
    result = fault_process(context, session, id, (FaultAction)action);
    printf("报告处理%s：%s。\n", result == FAULT_OK ? "成功" : "未确认成功", fault_result_message(result));
done:
    storage_free_reservations(&affected); return input;
}

/* 功能：报告过滤菜单。参数：管理员会话。返回：InputResult。 */
static InputResult report_list_flow(const AuthSession *session)
{
    show_reports(session, 0, REPORT_PENDING);
    for (;;) {
        int choice, no;
        InputResult input;
        puts("1. 待处理报告\n2. 已处理报告\n3. 指定电脑全部报告\n4. 全部报告\n0. 返回维护菜单");
        input = read_menu(4, &choice);
        if (input != INPUT_OK || choice == 0) { return input; }
        if (choice == 3) {
            input = utils_read_int_range("电脑编号：", 1, COMPUTER_COUNT, &no);
            if (input != INPUT_OK) { return input; }
            show_reports(session, no, -1);
        } else { show_reports(session, 0, choice == 4 ? -1 : (choice == 1 ? REPORT_PENDING : REPORT_PROCESSED)); }
    }
}

/* 功能：管理员维护二级菜单，每轮检查权限。参数：时钟/会话。返回：InputResult。 */
static InputResult maintenance_flow(const BookingContext *context, const AuthSession *session)
{
    for (;;) {
        int choice;
        InputResult input;
        FaultResult result = fault_require_role(session, ROLE_ADMIN);
        if (result != FAULT_OK) { printf("维护访问失败：%s。\n", fault_result_message(result)); return INPUT_OK; }
        puts("\n电脑状态维护\n1. 查看全部电脑状态\n2. 标记电脑故障\n3. 恢复电脑正常\n4. 查看待处理故障报告\n5. 处理故障报告\n0. 返回");
        input = read_menu(5, &choice);
        if (input != INPUT_OK || choice == 0) { return input; }
        if (choice == 1) { show_current_computers(); }
        else if (choice == 2 || choice == 3) { input = change_computer_flow(context, session, choice == 2 ? COMPUTER_FAULT : COMPUTER_NORMAL); }
        else if (choice == 4) { input = report_list_flow(session); }
        else { input = process_report_flow(context, session); }
        if (input == INPUT_EOF || input == INPUT_ERROR) { return input; }
        if (input == INPUT_INVALID) { puts("输入无效，本次维护操作已取消。"); }
    }
}

/* 功能：查询真实预约人数并展示关联学生。参数：管理员会话。返回：InputResult。 */
static InputResult admin_usage_flow(const AuthSession *session)
{
    AdminUsage usage;
    int date, slot;
    size_t i;
    FaultResult result;
    InputResult input = read_booking_target(&date, &slot);
    if (input != INPUT_OK) { return input; }
    result = admin_query_usage(session, date, slot, &usage);
    if (result != FAULT_OK) { printf("查询失败：%s。\n", fault_result_message(result)); return INPUT_OK; }
    puts("实验室预约情况（预约人数不代表签到或实际到场人数）：");
    printf("电脑总数：40；有效预约人数：%zu；空闲电脑：%d；故障电脑：%d。\n",
           usage.count, usage.seats.free_count, usage.seats.fault_count);
    print_booking_seats(date, slot, &usage.seats);
    if (usage.count == 0) { puts("暂无有效预约。"); }
    for (i = 0; i < usage.count; ++i) {
        printf("学号 %s | 姓名 %s | 电脑 %02d\n", usage.rows[i].student_id, usage.rows[i].name, usage.rows[i].computer_no);
    }
    return INPUT_OK;
}

/* 功能：精确学生查询，默认有效，可查看全部历史。参数：管理员会话。返回：InputResult。 */
static InputResult admin_student_flow(const AuthSession *session)
{
    char id[ID_LEN];
    AdminStudentInfo info = {0};
    FaultResult result;
    int choice;
    InputResult input = utils_read_field("精确学号：", id, sizeof id, 0);
    if (input != INPUT_OK) { return input; }
    result = admin_query_student(session, id, BOOKING_QUERY_ACTIVE, &info);
    if (result != FAULT_OK) { printf("查询失败：%s。\n", fault_result_message(result)); goto done; }
    printf("学号：%s；姓名：%s；有效预约：%zu；历史预约总数（含有效和已取消）：%zu。\n",
           info.student_id, info.name, info.active_count, info.history_count);
    puts("有效预约："); print_reservations(&info.records);
    puts("累计实验时间统计尚未开放（P4）。\n1. 查看全部历史预约\n0. 返回管理员菜单");
    input = read_menu(1, &choice);
    if (input != INPUT_OK || !choice) { goto done; }
    result = admin_query_student(session, id, BOOKING_QUERY_ALL, &info);
    if (result == FAULT_OK) { print_reservations(&info.records); }
    else { printf("查询失败：%s。\n", fault_result_message(result)); }
done:
    admin_free_student(&info); return input;
}

/* 功能：显示已认证角色菜单，进入及每轮均检查同一角色权限。
 * 参数：role 预期角色；session 当前会话；computers 座位；context 时钟。返回：InputResult。
 */
static InputResult user_menu(UserRole role, AuthSession *session,
                             const Lab computers[COMPUTER_COUNT], const BookingContext *context)
{
    (void)computers; /* 维护后必须刷新文件，不能复用启动快照。 */
    for (;;) {
        int choice; /* 当前角色菜单选项。 */
        InputResult input; /* EOF/ERROR 必须退出并清除会话。 */
        if (!auth_session_has_role(session, role)) {
            fputs("访问被拒绝：需要对应角色登录。\n", stderr);
            auth_logout(session);
            return INPUT_ERROR;
        }
        printf("\n------ %s菜单 ------\n当前%s：%s\n",
               role == ROLE_ADMIN ? "管理员" : "学生",
               role == ROLE_ADMIN ? "工号" : "学号", session->user_id);
        puts("1. 查看实验室座位");
        if (role == ROLE_ADMIN) {
            puts("2. 电脑状态维护（P3）\n3. 查询实验室使用情况（P3）");
            puts("4. 查询学生信息（P3）\n5. 统计实验室使用情况（P4）\n6. 退出登录");
        } else {
            puts("2. 预约电脑（P2）\n3. 查询我的预约（P2）\n4. 取消预约（P2）");
            puts("5. 报告电脑故障（P3）\n6. 查看累计实验时间（P4）\n7. 退出登录");
        }
        input = read_menu(role == ROLE_ADMIN ? 6 : 7, &choice);
        if (input != INPUT_OK) {
            auth_logout(session);
            return input;
        }
        if (choice == 0 || choice == (role == ROLE_ADMIN ? 6 : 7)) {
            auth_logout(session);
            puts("已退出登录，登录状态已清除。");
            return INPUT_OK;
        }
        if (role == ROLE_STUDENT && choice >= 1 && choice <= 4) {
            if (choice == 1 || choice == 2) { input = seat_booking_flow(context, session, choice == 2); }
            else if (choice == 3) { input = query_booking_flow(context, session); }
            else { input = cancel_booking_flow(context, session); }
            if (input == INPUT_EOF || input == INPUT_ERROR) { auth_logout(session); return input; }
            if (input == INPUT_INVALID) { puts("输入无效，本次预约操作已取消。"); }
        } else {
            if (role == ROLE_STUDENT && choice == 5) { input = submit_fault_flow(context, session); }
            else if (role == ROLE_ADMIN && choice == 1) { show_current_computers(); }
            else if (role == ROLE_ADMIN && choice == 2) { input = maintenance_flow(context, session); }
            else if (role == ROLE_ADMIN && choice == 3) { input = admin_usage_flow(session); }
            else if (role == ROLE_ADMIN && choice == 4) { input = admin_student_flow(session); }
            else { puts("该功能尚未开放"); }
            if (input == INPUT_EOF || input == INPUT_ERROR) { auth_logout(session); return input; }
            if (input == INPUT_INVALID) { puts("输入无效，本次操作已取消。"); }
        }
    }
}

/* 功能：收集角色凭据并在成功后进入角色菜单。
 * 参数：role 角色；session 输出；computers 座位；context 预约时钟。返回：InputResult。
 */
static InputResult login_user(UserRole role, AuthSession *session,
                              const Lab computers[COMPUTER_COUNT], const BookingContext *context)
{
    char id[ID_LEN] = {0}; /* 本次登录标识。 */
    char password[PASSWORD_LEN] = {0}; /* 本次登录密码，不传给会话。 */
    InputResult input; /* 字段输入结果。 */
    AuthResult result; /* 登录结果。 */
    auth_logout(session);
    input = utils_read_field(role == ROLE_ADMIN ? "工号：" : "学号：", id, sizeof id, 0);
    if (input != INPUT_OK) {
        goto done;
    }
    if (!utils_valid_id(id)) {
        input = INPUT_INVALID;
        goto done;
    }
    input = utils_read_field("密码：", password, sizeof password, 1);
    if (input != INPUT_OK) {
        goto done;
    }
    result = role == ROLE_ADMIN ? auth_login_admin(id, password, session) :
                                 auth_login_student(id, password, session);
    /* 在进入下一菜单前就擦除密码，避免它在会话期间仍留在此栈帧。 */
    utils_clear_secret(password, sizeof password);
    if (result == AUTH_OK) {
        puts("登录成功。");
        input = user_menu(role, session, computers, context);
    } else {
        printf("登录失败：%s。可重新选择登录或返回。\n", auth_result_message(result));
    }
done:
    if (input == INPUT_INVALID) {
        puts("输入无效：账户标识或密码格式/长度不合法，本次登录已取消。");
    }
    utils_clear_secret(password, sizeof password);
    auth_logout(session); /* 离开登录流程时绝不遗留会话，包括 EOF。 */
    return input;
}

/* 功能：角色入口只允许注册、登录和返回，不直接访问已登录菜单。
 * 参数：role 固定角色；session 调用方会话；computers 座位；context 时钟。返回：InputResult。
 */
static InputResult role_entry(UserRole role, AuthSession *session,
                              const Lab computers[COMPUTER_COUNT], const BookingContext *context)
{
    auth_logout(session);
    for (;;) {
        int choice; /* 本次入口选择。 */
        InputResult input; /* 子流程结果。 */
        printf("\n------ %s入口 ------\n", role == ROLE_ADMIN ? "管理员" : "学生");
        printf("1. %s注册\n2. %s登录\n3. 返回主菜单\n",
               role == ROLE_ADMIN ? "管理员" : "学生", role == ROLE_ADMIN ? "管理员" : "学生");
        input = read_menu(3, &choice);
        if (input != INPUT_OK) {
            return input;
        }
        if (choice == 0 || choice == 3) {
            auth_logout(session);
            return INPUT_OK;
        }
        input = choice == 1 ? register_user(role) : login_user(role, session, computers, context);
        if (input == INPUT_EOF || input == INPUT_ERROR) {
            auth_logout(session);
            return input;
        }
    }
}

/* 功能：主菜单持有显式会话，所有退出路径统一清理；参数、返回值见 ui.h。 */
int ui_run(const Lab computers[COMPUTER_COUNT])
{
    return ui_run_with_context(computers, NULL);
}

/* 功能：持有会话并传递可注入时钟；参数、返回值见 ui.h。 */
int ui_run_with_context(const Lab computers[COMPUTER_COUNT], const BookingContext *context)
{
    AuthSession session = {0}; /* 无全局会话，不能靠切菜单继承权限。 */
    InputResult input = INPUT_OK; /* 用于统一 EOF/错误退出。 */
    for (;;) {
        int choice; /* 主菜单选项。 */
        puts("========================================");
        puts("         智能实验室预约系统");
        puts("========================================\n");
        puts("        1. 管理员入口");
        puts("        2. 学生入口");
        puts("        3. 查看实验室座位图");
        puts("        4. 退出系统（兼容输入 0）\n");
        puts("========================================");
        input = read_menu(4, &choice);
        if (input != INPUT_OK) {
            break;
        }
        if (choice == 0 || choice == 4) {
            puts("感谢使用，再见！");
            break;
        }
        if (choice == 3) {
            show_current_computers();
        } else {
            input = role_entry(choice == 1 ? ROLE_ADMIN : ROLE_STUDENT, &session, computers, context);
        }
        if (input == INPUT_EOF || input == INPUT_ERROR) {
            break;
        }
    }
    auth_logout(&session);
    if (input == INPUT_EOF) {
        puts("\n输入结束，退出系统。");
    } else if (input == INPUT_ERROR) {
        fputs("输入或输出流错误，程序退出。\n", stderr);
        return 0;
    }
    return 1;
}
