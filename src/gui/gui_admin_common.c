#include "gui_admin.h"
#include "gui_theme.h"
#include "gui_widgets.h"
#include "utils.h"
#include <wchar.h>

/* 所有管理员入口再检查角色、实际账户及 P3 门禁，不能仅信导航可见性。 */
int gui_admin_allowed(GuiApp *app)
{
    FaultResult result=fault_require_role(&app->session,ROLE_ADMIN);
    if (result!=FAULT_OK) { gui_admin_error(app,result); return 0; }
    return 1;
}
void gui_admin_error(GuiApp *app, FaultResult result)
{
    if (result==FAULT_RECOVERY_REQUIRED) {
        gui_notice(app,L"维护未完成，本次操作未确认成功，相关修改已暂停。请先完整备份 data/，按现有恢复流程重新启动；不要删除维护日志。",1);
        EnableWindow(GetDlgItem(app->window,GUI_ADMIN_MARK),FALSE);
        EnableWindow(GetDlgItem(app->window,GUI_ADMIN_RESTORE),FALSE);
        EnableWindow(GetDlgItem(app->window,GUI_ADMIN_PROCESS),FALSE);
        EnableWindow(GetDlgItem(app->window,GUI_ADMIN_EXPORT),FALSE);
    } else { gui_notice_utf8(app,fault_result_message(result),1); }
}
void gui_admin_title(GuiApp *app, HDC dc, const wchar_t *title, const wchar_t *subtitle)
{
    gui_text(app,dc,title,256,100,app->width-470,44,app->heading,GUI_INK,0);
    gui_text(app,dc,subtitle,256,148,app->width-288,42,app->body,GUI_MUTED,DT_WORDBREAK);
}
HWND gui_date_control(GuiApp *app, int id)
{
    SYSTEMTIME range[2]={{0},{0}}, time={0};
    LabTime now;
    HWND control=gui_control(app,DATETIMEPICK_CLASSW,L"",WS_TABSTOP | DTS_SHORTDATEFORMAT,id);
    range[0].wYear=1000; range[0].wMonth=1; range[0].wDay=1;
    range[1].wYear=9999; range[1].wMonth=12; range[1].wDay=31;
    SendMessageW(control,DTM_SETFORMATW,0,(LPARAM)L"yyyy-MM-dd");
    SendMessageW(control,DTM_SETRANGE,GDTR_MIN | GDTR_MAX,(LPARAM)range);
    if (utils_local_time(NULL,&now)) {
        time.wYear=(WORD)(now.date/10000); time.wMonth=(WORD)(now.date/100%100); time.wDay=(WORD)(now.date%100);
    } else { GetLocalTime(&time); }
    SendMessageW(control,DTM_SETSYSTEMTIME,GDT_VALID,(LPARAM)&time);
    return control;
}
HWND gui_slot_control(GuiApp *app, int id)
{
    int i, slot=0;
    LabTime now;
    wchar_t text[32];
    HWND control=gui_control(app,L"COMBOBOX",L"",WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,id);
    for (i=0;i<SLOT_COUNT;++i) {
        swprintf(text,32,L"%02d:00–%02d:00",OPEN_HOUR+i,OPEN_HOUR+i+1);
        SendMessageW(control,CB_ADDSTRING,0,(LPARAM)text);
    }
    if (utils_local_time(NULL,&now) && now.hour>=OPEN_HOUR && now.hour<CLOSE_HOUR) { slot=now.hour-OPEN_HOUR; }
    SendMessageW(control,CB_SETCURSEL,(WPARAM)slot,0);
    return control;
}
int gui_read_date(HWND control)
{
    SYSTEMTIME time={0};
    if (SendMessageW(control,DTM_GETSYSTEMTIME,0,(LPARAM)&time)!=GDT_VALID) { return 0; }
    return time.wYear*10000+time.wMonth*100+time.wDay;
}
void gui_admin_current(GuiApp *app)
{
    LabTime now;
    app->usage_valid=0; app->current_open=0; app->usage_result=FAULT_CLOCK_ERROR;
    if (!utils_local_time(NULL,&now)) { return; }
    app->date=now.date; app->slot=now.hour-OPEN_HOUR;
    if (now.hour<OPEN_HOUR || now.hour>=CLOSE_HOUR) { app->usage_result=FAULT_OK; return; }
    app->current_open=1;
    app->usage_result=admin_query_usage(&app->session,app->date,app->slot,&app->usage);
    app->usage_valid=app->usage_result==FAULT_OK;
}
void gui_admin_computers(GuiApp *app)
{
    int i;
    app->computers_valid=0; app->normal_count=app->fault_count=0;
    app->computers_result=fault_read_computers(&app->session,app->computers);
    if (app->computers_result!=FAULT_OK) { return; }
    for (i=0;i<COMPUTER_COUNT;++i) {
        if (app->computers[i].status==COMPUTER_NORMAL) { ++app->normal_count; }
        else { ++app->fault_count; }
    }
    app->computers_valid=1;
}
void gui_admin_reservation_cell(const Reservation *row, int column, wchar_t *text, size_t capacity)
{
    switch (column) {
    case 0: swprintf(text,capacity,L"%d",row->id); break;
    case 1: swprintf(text,capacity,L"%04d-%02d-%02d",row->date/10000,row->date/100%100,row->date%100); break;
    case 2: swprintf(text,capacity,L"%02d:00–%02d:00",OPEN_HOUR+row->slot_index,OPEN_HOUR+row->slot_index+1); break;
    case 3: swprintf(text,capacity,L"%02d",row->computer_no); break;
    default: swprintf(text,capacity,L"%ls",row->status==BOOKING_ACTIVE ? L"有效" : L"已取消"); break;
    }
}
/* 使用情况只绘制已缓存投影，故障符号优先，人数另外取 AdminUsage.count。 */
void gui_admin_grid(GuiApp *app, HDC dc, int x, int y, int width, int height)
{
    int i, cell=(width-7*6)/8, row=(height-4*8)/5;
    for (i=0;i<COMPUTER_COUNT;++i) {
        char symbol=app->usage_valid ? app->usage.seats.symbols[i] : '?';
        wchar_t text[20];
        COLORREF color=symbol=='!' ? RGB(254,226,226) : symbol=='*' ? RGB(220,252,231) : RGB(226,232,240);
        swprintf(text,20,L"%02d\n%ls",i+1,symbol=='!' ? L"故障" : symbol=='*' ? L"空闲" : symbol=='#' ? L"预约" : L"未知");
        gui_card(app,dc,x+i%8*(cell+6),y+i/8*(row+8),cell,row,color);
        gui_text(app,dc,text,x+i%8*(cell+6),y+i/8*(row+8)+4,cell,row-4,app->small,symbol=='!' ? GUI_RED : GUI_INK,DT_CENTER);
    }
}

/* 确认窗口独立拥有字体和控件；只借用预览集合，调用者唯一释放。 */
typedef struct {
    GuiApp ui;
    const wchar_t *summary;
    const ReservationList *affected;
    int done, answer;
} Confirmation;
static void confirmation_layout(Confirmation *state)
{
    RECT rect;
    const int weights[]={14,22,22,26,16};
    GuiApp *ui=&state->ui;
    GetClientRect(ui->window,&rect);
    ui->width=MulDiv(rect.right,96,(int)ui->dpi); ui->height=MulDiv(rect.bottom,96,(int)ui->dpi);
    gui_move(ui,GUI_ADMIN_PREVIEW_TABLE,24,168,ui->width-48,ui->height-250);
    gui_move(ui,GUI_ADMIN_NO,ui->width-320,ui->height-60,136,38);
    gui_move(ui,GUI_ADMIN_YES,ui->width-168,ui->height-60,144,38);
    gui_table_layout(ui,weights,5);
    InvalidateRect(ui->window,NULL,TRUE);
}
static LRESULT CALLBACK confirmation_proc(HWND window, UINT message, WPARAM wp, LPARAM lp)
{
    Confirmation *state=(Confirmation *)GetWindowLongPtrW(window,GWLP_USERDATA);
    if (message==WM_NCCREATE) {
        state=((CREATESTRUCTW *)lp)->lpCreateParams;
        state->ui.window=window; SetWindowLongPtrW(window,GWLP_USERDATA,(LONG_PTR)state);
    }
    if (!state) { return DefWindowProcW(window,message,wp,lp); }
    switch (message) {
    case WM_CREATE: {
        const wchar_t *columns[]={L"预约编号",L"日期",L"时段",L"学号",L"电脑"};
        if (!gui_theme_create(&state->ui)) { return -1; }
        state->ui.table=gui_table(&state->ui,GUI_ADMIN_PREVIEW_TABLE,columns,5);
        gui_button(&state->ui,L"否，返回",GUI_ADMIN_NO);
        gui_button(&state->ui,L"是，确认执行",GUI_ADMIN_YES);
        ListView_SetItemCountEx(state->ui.table,state->affected ? (int)state->affected->count : 0,0);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wp)==GUI_ADMIN_YES || LOWORD(wp)==GUI_ADMIN_NO || LOWORD(wp)==IDCANCEL) {
            state->answer=LOWORD(wp)==GUI_ADMIN_YES;
            DestroyWindow(window);
        }
        return 0;
    case WM_NOTIFY: {
        NMLVDISPINFOW *info=(NMLVDISPINFOW *)lp;
        if (info->hdr.code==LVN_GETDISPINFOW && state->affected && info->item.iItem>=0 &&
            (size_t)info->item.iItem<state->affected->count) {
            wchar_t text[128];
            const Reservation *row=&state->affected->items[info->item.iItem];
            if (info->item.iSubItem==3) { gui_wide(row->student_id,text,128); }
            else { gui_admin_reservation_cell(row,info->item.iSubItem==4 ? 3 : info->item.iSubItem,text,128); }
            gui_table_text(info,text);
        }
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT paint;
        HDC dc=BeginPaint(window,&paint);
        FillRect(dc,&paint.rcPaint,state->ui.background);
        gui_text(&state->ui,dc,L"请确认本次操作",24,20,state->ui.width-48,40,state->ui.heading,GUI_INK,0);
        gui_text(&state->ui,dc,state->summary,24,72,state->ui.width-48,88,state->ui.body,GUI_INK,DT_WORDBREAK);
        EndPaint(window,&paint); return 0;
    }
    case WM_DRAWITEM: gui_draw_button(&state->ui,(DRAWITEMSTRUCT *)lp); return TRUE;
    case WM_SIZE: confirmation_layout(state); return 0;
    case WM_DPICHANGED: {
        size_t i;
        RECT rect=*(RECT *)lp;
        gui_theme_destroy(&state->ui); state->ui.dpi=HIWORD(wp);
        if (!gui_theme_create(&state->ui)) { DestroyWindow(window); return 0; }
        for (i=0;i<state->ui.child_count;++i) { SendMessageW(state->ui.children[i],WM_SETFONT,(WPARAM)state->ui.body,TRUE); }
        SetWindowPos(window,NULL,rect.left,rect.top,rect.right-rect.left,rect.bottom-rect.top,SWP_NOZORDER);
        confirmation_layout(state); return 0;
    }
    case WM_CLOSE: DestroyWindow(window); return 0;
    case WM_DESTROY:
        gui_clear_controls(&state->ui); gui_theme_destroy(&state->ui); state->done=1; return 0;
    }
    return DefWindowProcW(window,message,wp,lp);
}
int gui_admin_confirm(GuiApp *app, const wchar_t *summary, const ReservationList *affected)
{
    Confirmation state={0};
    WNDCLASSW type={0};
    RECT parent, rect={0,0,gui_scale(app,820),gui_scale(app,500)};
    HWND window;
    MSG message;
    state.ui.instance=app->instance; state.ui.dpi=app->dpi;
    state.summary=summary; state.affected=affected;
    type.lpfnWndProc=confirmation_proc; type.hInstance=app->instance;
    type.lpszClassName=L"LabSlotAdminConfirm"; type.hCursor=LoadCursorW(NULL,IDC_ARROW);
    if (!RegisterClassW(&type) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS) { return 0; }
    GetWindowRect(app->window,&parent);
    AdjustWindowRectEx(&rect,WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,FALSE,WS_EX_DLGMODALFRAME);
    window=CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT,type.lpszClassName,L"LabSlot · 管理员操作确认",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN,
        parent.left+(parent.right-parent.left-(rect.right-rect.left))/2,
        parent.top+(parent.bottom-parent.top-(rect.bottom-rect.top))/2,
        rect.right-rect.left,rect.bottom-rect.top,app->window,NULL,app->instance,&state);
    if (!window) { gui_theme_destroy(&state.ui); return 0; }
    EnableWindow(app->window,FALSE); ShowWindow(window,SW_SHOW); SetFocus(GetDlgItem(window,GUI_ADMIN_NO));
    while (!state.done) {
        int result=(int)GetMessageW(&message,NULL,0,0);
        if (result<=0) {
            state.answer=0; DestroyWindow(window);
            if (!result) { PostQuitMessage((int)message.wParam); }
            break;
        }
        if (message.message==WM_KEYDOWN && (message.wParam==VK_RETURN || message.wParam==VK_ESCAPE)) {
            SendMessageW(window,WM_COMMAND,message.wParam==VK_RETURN && GetFocus()==GetDlgItem(window,GUI_ADMIN_YES) ? GUI_ADMIN_YES : GUI_ADMIN_NO,0);
        } else if (!IsDialogMessageW(window,&message)) { TranslateMessage(&message); DispatchMessageW(&message); }
    }
    if (IsWindow(app->window)) { EnableWindow(app->window,TRUE); SetActiveWindow(app->window); }
    return state.answer;
}
