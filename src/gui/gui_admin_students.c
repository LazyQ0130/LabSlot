#include "gui_admin.h"
#include "gui_theme.h"
#include "gui_widgets.h"
#include <wchar.h>

void gui_admin_students_query(GuiApp *app)
{
    char id[ID_LEN];
    BookingQuery mode=SendMessageW(app->filter,CB_GETCURSEL,0,0)==1 ? BOOKING_QUERY_ALL : BOOKING_QUERY_ACTIVE;
    app->admin_student_valid=0;
    ListView_SetItemCountEx(app->table,0,0); admin_free_student(&app->admin_student);
    if (!gui_admin_allowed(app)) { return; }
    if (!gui_read_edit(app->id,id,sizeof id)) { gui_notice(app,L"请输入合法完整学号进行精确查询。",1); return; }
    app->admin_student_result=admin_query_student(&app->session,id,mode,&app->admin_student);
    app->admin_student_valid=app->admin_student_result==FAULT_OK;
    if (app->admin_student_valid) {
        ListView_SetItemCountEx(app->table,(int)app->admin_student.records.count,0);
        gui_notice(app,L"查询成功。仅显示基本信息与预约事实，不显示密码或累计时间缓存。",0);
    } else { gui_admin_error(app,app->admin_student_result); }
    InvalidateRect(app->window,NULL,FALSE);
}
void gui_admin_students_create(GuiApp *app)
{
    const wchar_t *columns[]={L"预约编号",L"日期",L"时间段",L"电脑",L"状态"};
    app->id=gui_edit(app,GUI_ADMIN_STUDENT_ID,0,ID_LEN-1);
    SendMessageW(app->id,EM_SETCUEBANNER,TRUE,(LPARAM)L"请输入完整学号");
    app->filter=gui_control(app,L"COMBOBOX",L"",WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,GUI_ADMIN_STUDENT_MODE);
    SendMessageW(app->filter,CB_ADDSTRING,0,(LPARAM)L"有效预约");
    SendMessageW(app->filter,CB_ADDSTRING,0,(LPARAM)L"全部历史");
    SendMessageW(app->filter,CB_SETCURSEL,0,0);
    gui_button(app,L"查询 / 刷新",GUI_ADMIN_QUERY);
    app->table=gui_table(app,GUI_ADMIN_TABLE,columns,5);
    app->admin_student_valid=0;
    gui_notice(app,L"输入完整学号，按 Enter 或点击查询。",0);
}
void gui_admin_students_layout(GuiApp *app)
{
    const int weights[]={16,24,26,14,20};
    gui_move(app,GUI_ADMIN_STUDENT_ID,272,204,240,38);
    gui_move(app,GUI_ADMIN_STUDENT_MODE,528,204,160,200);
    gui_move(app,GUI_ADMIN_QUERY,app->width-192,204,144,38);
    gui_move(app,GUI_ADMIN_TABLE,272,340,app->width-320,app->height-440);
    gui_table_layout(app,weights,5);
}
void gui_admin_students_paint(GuiApp *app, HDC dc)
{
    wchar_t text[240], student[ID_LEN], name[NAME_LEN];
    gui_admin_title(app,dc,L"学生信息查询",L"按完整学号精确查询基本信息与预约明细。所有查询使用管理员权限。");
    gui_card(app,dc,256,190,app->width-288,app->height-270,RGB(255,255,255));
    if (app->admin_student_valid) {
        gui_wide(app->admin_student.student_id,student,ID_LEN); gui_wide(app->admin_student.name,name,NAME_LEN);
        swprintf(text,240,L"学号 %ls    姓名 %ls\n有效预约 %zu 条    全部历史 %zu 条    当前显示 %zu 条",
            student,name,app->admin_student.active_count,app->admin_student.history_count,app->admin_student.records.count);
    } else { wcscpy(text,L"尚无查询结果。输入学号后查询；失败时清除上一名学生的数据。"); }
    gui_text(app,dc,text,272,266,app->width-320,64,app->body,GUI_INK,DT_WORDBREAK);
}
LRESULT gui_admin_students_notify(GuiApp *app, NMHDR *header)
{
    if (header->idFrom==GUI_ADMIN_TABLE && header->code==LVN_GETDISPINFOW) {
        NMLVDISPINFOW *info=(NMLVDISPINFOW *)header;
        wchar_t text[96]={0};
        if (app->admin_student_valid && info->item.iItem>=0 && (size_t)info->item.iItem<app->admin_student.records.count) {
            gui_admin_reservation_cell(&app->admin_student.records.items[info->item.iItem],info->item.iSubItem,text,96);
        }
        gui_table_text(info,text);
    }
    return 0;
}
