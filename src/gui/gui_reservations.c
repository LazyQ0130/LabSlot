#include "gui_reservations.h"
#include "gui_widgets.h"
#include "gui_theme.h"
#include "gui_student_stats.h"
#include <wchar.h>

/* 功能：返回当前选择的自有记录；参数应用；无有效选择返回 NULL。 */
static const Reservation *selected_record(GuiApp *app)
{
    int row=app->table ? ListView_GetNextItem(app->table,-1,LVNI_SELECTED) : -1;
    if (!app->reservations_valid || row<0 || (size_t)row>=app->reservations.count) { return NULL; }
    return &app->reservations.items[row];
}
/* 功能：显示提示所需的时间判断；参数应用/记录；最终仍须业务层预检。 */
static int appears_cancellable(GuiApp *app, const Reservation *record)
{
    return record && record->status==BOOKING_ACTIVE &&
           utils_slot_future(&app->reservations_as_of,record->date,record->slot_index);
}
/* 功能：更新选择相关按钮；参数应用；无返回。 */
static void update_selection(GuiApp *app)
{
    const Reservation *record=selected_record(app);
    EnableWindow(GetDlgItem(app->window,GUI_RES_DETAILS),record && !app->busy);
    EnableWindow(GetDlgItem(app->window,GUI_RES_CANCEL),!app->busy && appears_cancellable(app,record));
}
/* 功能：重新查询当前筛选并丢弃旧投影；参数应用；结果保存在应用中。 */
void gui_reservations_refresh(GuiApp *app)
{
    int mode=(int)SendMessageW(app->filter,CB_GETCURSEL,0,0);
    wchar_t message[96];
    app->reservations_valid=0;
    ListView_SetItemCountEx(app->table,0,LVSICF_NOSCROLL);
    storage_free_reservations(&app->reservations);
    app->reservations_result=booking_query(NULL,&app->session,(BookingQuery)mode,&app->reservations);
    if (app->reservations_result==BOOKING_OK) {
        app->reservations_valid=1;
        if (!utils_local_time(NULL,&app->reservations_as_of)) {
            app->reservations_as_of=(LabTime){0}; /* 无时钟时不启用取消提示，业务层仍会返回时钟错误。 */
        }
        ListView_SetItemCountEx(app->table,(int)app->reservations.count,LVSICF_NOSCROLL);
        swprintf(message,96,L"已读取本人预约，共 %zu 条。选择记录可查看详情。",app->reservations.count);
        gui_notice(app,message,0);
    } else { gui_notice_utf8(app,booking_result_message(app->reservations_result),1); }
    ListView_SetItemState(app->table,-1,0,LVIS_SELECTED | LVIS_FOCUSED);
    update_selection(app); InvalidateRect(app->window,NULL,FALSE);
}
/* 功能：建立本人预约控件并查询；参数应用；无返回。 */
void gui_reservations_create(GuiApp *app)
{
    const wchar_t *columns[]={L"预约编号",L"预约日期",L"时间段",L"电脑",L"状态",L"操作"};
    app->filter=gui_control(app,L"COMBOBOX",L"",WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,GUI_RES_FILTER);
    SendMessageW(app->filter,CB_ADDSTRING,0,(LPARAM)L"有效预约");
    SendMessageW(app->filter,CB_ADDSTRING,0,(LPARAM)L"全部预约历史");
    SendMessageW(app->filter,CB_ADDSTRING,0,(LPARAM)L"可取消预约");
    SendMessageW(app->filter,CB_SETCURSEL,0,0);
    gui_button(app,L"刷新列表",GUI_RES_REFRESH);
    app->table=gui_table(app,GUI_RES_TABLE,columns,6);
    gui_button(app,L"查看详情",GUI_RES_DETAILS);
    gui_button(app,L"取消预约",GUI_RES_CANCEL);
    gui_reservations_refresh(app);
}
/* 功能：缩放表格与操作区；参数应用；无返回。 */
void gui_reservations_layout(GuiApp *app)
{
    const int weights[]={12,20,24,14,14,16};
    gui_move(app,GUI_RES_FILTER,280,202,210,240);
    gui_move(app,GUI_RES_REFRESH,510,202,132,38);
    gui_move(app,GUI_RES_TABLE,280,288,app->width-336,app->height-450);
    gui_move(app,GUI_RES_DETAILS,app->width-370,app->height-138,146,42);
    gui_move(app,GUI_RES_CANCEL,app->width-204,app->height-138,148,42);
    gui_table_layout(app,weights,6);
}
/* 功能：绘制标题和明确的空/错误状态；参数应用/DC；无返回，不加载文件。 */
void gui_reservations_paint(GuiApp *app, HDC dc)
{
    wchar_t summary[100];
    gui_text(app,dc,L"我的预约",256,100,app->width-288,44,app->heading,GUI_INK,0);
    gui_text(app,dc,L"查看本人预约记录，选择尚未开始的预约进行取消。",256,146,app->width-288,28,app->body,GUI_MUTED,0);
    gui_card(app,dc,256,182,app->width-288,app->height-262,RGB(255,255,255));
    if (!app->reservations_valid) { wcscpy(summary,L"预约读取失败，请检查错误提示后刷新；未展示旧数据。"); }
    else if (!app->reservations.count) { wcscpy(summary,L"这个筛选下还没有预约，可以前往“座位预约”选择电脑。"); }
    else { swprintf(summary,100,L"共 %zu 条本人记录  ·  双击记录查看详情",app->reservations.count); }
    gui_text(app,dc,summary,280,253,app->width-336,28,app->body,app->reservations_valid ? GUI_MUTED : GUI_RED,0);
    gui_text(app,dc,L"取消仅适用于本人未开始的有效预约。",280,app->height-136,app->width-680,44,
             app->small,GUI_MUTED,DT_WORDBREAK);
}
/* 功能：格式化无其他学生信息的预约详情；参数记录/输出/容量；无返回。 */
static void describe(const Reservation *record, wchar_t *text, size_t capacity)
{
    swprintf(text,capacity,L"预约编号：%d\n日期：%04d-%02d-%02d\n时间：%02d:00–%02d:00\n电脑：%02d\n状态：%ls",
        record->id,record->date/10000,record->date/100%100,record->date%100,
        OPEN_HOUR+record->slot_index,OPEN_HOUR+record->slot_index+1,record->computer_no,
        record->status==BOOKING_ACTIVE ? L"有效" : L"已取消");
}
/* 功能：预检和确认后取消本人记录；参数应用；失败显示原业务错误。 */
static void cancel_selected(GuiApp *app)
{
    const Reservation *selected=selected_record(app);
    Reservation record={0};
    BookingResult result;
    int id;
    wchar_t details[320], prompt[448], error[180];
    if (!selected || !auth_session_has_role(&app->session,ROLE_STUDENT)) { return; }
    id=selected->id; app->busy=1; update_selection(app);
    result=booking_check_cancel(NULL,&app->session,id,&record);
    if (result!=BOOKING_OK) {
        gui_reservations_refresh(app); gui_notice_utf8(app,booking_result_message(result),1); goto done;
    }
    describe(&record,details,320);
    swprintf(prompt,448,L"%ls\n\n确认取消此预约？此操作会释放座位。",details);
    if (MessageBoxW(app->window,prompt,L"确认取消预约",MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2)!=IDYES) {
        gui_notice(app,L"已保留预约，未执行取消。",0); goto done;
    }
    result=booking_cancel(NULL,&app->session,id);
    gui_reservations_refresh(app);
    if (result==BOOKING_OK) {
        gui_student_stats_refresh(app); app->seats_valid=0; app->selected=0;
        if (app->reservations_result==BOOKING_OK) {
            swprintf(prompt,448,L"预约 %d 已取消，列表及个人统计已重新查询。",id);
            gui_notice(app,prompt,0);
        } else {
            gui_wide(booking_result_message(app->reservations_result),error,180);
            swprintf(prompt,448,L"取消已保存，但列表刷新失败：%ls",error); gui_notice(app,prompt,1);
        }
    } else { gui_notice_utf8(app,booking_result_message(result),1); }
done:
    app->busy=0; update_selection(app);
}
/* 功能：处理刷新、筛选、详情和取消；参数应用/编号/通知；无返回。 */
void gui_reservations_command(GuiApp *app, int id, int code)
{
    if (app->busy || app->page!=GUI_RESERVATIONS) { return; }
    if ((id==GUI_RES_FILTER && code==CBN_SELCHANGE) || (id==GUI_RES_REFRESH && code==BN_CLICKED)) {
        gui_reservations_refresh(app);
    } else if (id==GUI_RES_CANCEL && code==BN_CLICKED) { cancel_selected(app); }
    else if (id==GUI_RES_DETAILS && code==BN_CLICKED) {
        const Reservation *record=selected_record(app);
        wchar_t text[320];
        if (!record) { return; }
        describe(record,text,320); app->busy=1;
        MessageBoxW(app->window,text,L"预约详情",MB_OK | MB_ICONINFORMATION); app->busy=0;
    }
}
/* 功能：按需提供虚拟表格文本及处理原生选择；参数应用/通知；返回消息结果。 */
LRESULT gui_reservations_notify(GuiApp *app, NMHDR *header)
{
    if (header->idFrom!=GUI_RES_TABLE) { return 0; }
    if (header->code==LVN_GETDISPINFOW) {
        NMLVDISPINFOW *info=(NMLVDISPINFOW *)header;
        wchar_t text[100]=L"";
        if (app->reservations_valid && info->item.iItem>=0 && (size_t)info->item.iItem<app->reservations.count) {
            const Reservation *record=&app->reservations.items[info->item.iItem];
            switch (info->item.iSubItem) {
            case 0: swprintf(text,100,L"%d",record->id); break;
            case 1: swprintf(text,100,L"%04d-%02d-%02d",record->date/10000,record->date/100%100,record->date%100); break;
            case 2: swprintf(text,100,L"%02d:00–%02d:00",OPEN_HOUR+record->slot_index,OPEN_HOUR+record->slot_index+1); break;
            case 3: swprintf(text,100,L"%02d 号",record->computer_no); break;
            case 4: wcscpy(text,record->status==BOOKING_ACTIVE ? L"有效" : L"已取消"); break;
            case 5: wcscpy(text,appears_cancellable(app,record) ? L"可取消" : L"查看详情"); break;
            }
        }
        gui_table_text(info,text);
    } else if (!app->busy && header->code==LVN_ITEMCHANGED) { update_selection(app); }
    else if (!app->busy && header->code==NM_DBLCLK) { gui_reservations_command(app,GUI_RES_DETAILS,BN_CLICKED); }
    return 0;
}
