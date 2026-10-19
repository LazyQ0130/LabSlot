#ifndef LABSLOT_GUI_APP_H
#define LABSLOT_GUI_APP_H

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <commctrl.h>
#include "booking.h"
#include "stats.h"
#include "fault.h"
#include "admin.h"

/* 控件编号稳定，供消息分发及独立 Windows 集成测试使用。 */
enum {
    GUI_STUDENT = 100, GUI_ADMIN, GUI_ID, GUI_NAME, GUI_PASSWORD,
    GUI_CONFIRM, GUI_SUBMIT, GUI_SWITCH, GUI_HOME, GUI_BOOKING,
    GUI_LOGOUT, GUI_QUICK, GUI_DATE, GUI_SLOT, GUI_REFRESH, GUI_RESERVE,
    GUI_SEAT_FIRST = 200,
    GUI_NAV_RESERVATIONS = 300, GUI_NAV_FAULT, GUI_NAV_STATS,
    GUI_RES_FILTER, GUI_RES_TABLE, GUI_RES_REFRESH, GUI_RES_DETAILS, GUI_RES_CANCEL,
    GUI_FAULT_COMPUTER, GUI_FAULT_DETAIL, GUI_FAULT_SUBMIT, GUI_FAULT_REFRESH,
    GUI_REPORT_FILTER, GUI_REPORT_TABLE, GUI_REPORT_DETAILS, GUI_STATS_REFRESH,
    GUI_HOME_REFRESH, GUI_QUICK_RESERVATIONS, GUI_QUICK_FAULT,
    GUI_NAV_ADMIN_LAB = 400, GUI_NAV_ADMIN_REPORTS, GUI_NAV_ADMIN_USAGE,
    GUI_NAV_ADMIN_STUDENTS, GUI_NAV_ADMIN_STATS, GUI_ADMIN_REFRESH,
    GUI_ADMIN_QUICK_LAB, GUI_ADMIN_QUICK_REPORTS, GUI_ADMIN_QUICK_STATS,
    GUI_ADMIN_REPORT_STATUS, GUI_ADMIN_REPORT_PC, GUI_ADMIN_REPORT_ACTION,
    GUI_ADMIN_DETAILS, GUI_ADMIN_PROCESS, GUI_ADMIN_MARK, GUI_ADMIN_RESTORE,
    GUI_ADMIN_DATE, GUI_ADMIN_SLOT, GUI_ADMIN_TABLE, GUI_ADMIN_STUDENT_ID,
    GUI_ADMIN_STUDENT_MODE, GUI_ADMIN_QUERY, GUI_ADMIN_STATS_MODE,
    GUI_ADMIN_STATS_DATE, GUI_ADMIN_EXPORT, GUI_ADMIN_YES, GUI_ADMIN_NO,
    GUI_ADMIN_PREVIEW_TABLE,
    GUI_ADMIN_SEAT_FIRST = 500
};
typedef enum {
    GUI_LOGIN, GUI_REGISTER, GUI_DASHBOARD, GUI_SEATS,
    GUI_RESERVATIONS, GUI_FAULT, GUI_STUDENT_STATS,
    GUI_ADMIN_LAB, GUI_ADMIN_REPORTS, GUI_ADMIN_USAGE, GUI_ADMIN_STUDENTS, GUI_ADMIN_STATS
} GuiPage;

/* 单窗口持有会话和展示投影；不持有账户表，不直接读写业务 TXT。 */
typedef struct {
    HWND window;                    /* 顶层窗口。 */
    HINSTANCE instance;
    UINT dpi;                       /* 当前窗口 DPI，所有布局以 96 DPI 为基准。 */
    int width, height;              /* 逻辑客户区尺寸。 */
    HFONT body, small, heading, hero, number;
    HBRUSH background, white;
    GuiPage page;
    UserRole role;                  /* 认证页选择的角色，不等同于会话权限。 */
    AuthSession session;
    HWND children[64];              /* 页面控件，页面切换时逐个销毁。 */
    size_t child_count;
    HWND id, name, password, confirmation, date_picker, slot_picker;
    BookingSeats seats;
    int seats_valid, selected, date, slot, busy;
    StudentStats student_stats;
    StatsResult stats_result;
    wchar_t notice[320];            /* 不含凭据的内联提示。 */
    int notice_error;
    int changing_page;              /* 销毁/创建控件时忽略选择通知，防止访问旧集合。 */
    HWND table, filter, fault_computer, fault_detail;
    ReservationList reservations;   /* 当前预约页独占；离页或刷新逐一释放。 */
    FaultReportList reports;        /* 当前报告页独占；不与其他对象浅复制。 */
    BookingResult reservations_result;
    FaultResult reports_result, computers_result;
    int reservations_valid, reports_valid;
    LabTime reservations_as_of;     /* 仅用于显示可取消提示，最终由业务层复查。 */
    Lab computers[COMPUTER_COUNT];
    int computers_valid, normal_count, fault_count, current_open;
    AdminUsage usage;
    FaultResult usage_result, admin_student_result;
    int usage_valid, admin_student_valid, admin_stats_valid;
    AdminStudentInfo admin_student; /* 唯一所有者，离页及查询失败 admin_free_student。 */
    DailyStats daily;
    WeeklyStats weekly;
    int stats_mode;
    size_t pending_reports;
    HWND action;
} GuiApp;

int gui_scale(const GuiApp *app, int value);
void gui_move(GuiApp *app, int id, int x, int y, int width, int height);
void gui_notice(GuiApp *app, const wchar_t *text, int error);
void gui_notice_utf8(GuiApp *app, const char *text, int error);
int gui_utf8(const wchar_t *text, char *output, size_t capacity);
int gui_wide(const char *text, wchar_t *output, size_t capacity);
void gui_show_page(GuiApp *app, GuiPage page);
void gui_layout(GuiApp *app);
HWND gui_create(GuiApp *app, HINSTANCE instance);
void gui_enable_dpi(void);
void gui_update_dpi(GuiApp *app, UINT dpi);
int gui_set_working_directory(void);
int gui_initialize_data(wchar_t *error, size_t capacity);
int gui_translate_message(GuiApp *app, MSG *message);

#endif
