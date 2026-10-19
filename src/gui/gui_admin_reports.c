#include "gui_admin.h"
#include "gui_theme.h"
#include "gui_widgets.h"
#include <wchar.h>

static const FaultReport *selected(GuiApp *app)
{
    int row=ListView_GetNextItem(app->table,-1,LVNI_SELECTED);
    return app->reports_valid && row>=0 && (size_t)row<app->reports.count ? &app->reports.items[row] : NULL;
}
static void selection(GuiApp *app)
{
    const FaultReport *report=selected(app);
    EnableWindow(GetDlgItem(app->window,GUI_ADMIN_DETAILS),report && !app->busy);
    EnableWindow(GetDlgItem(app->window,GUI_ADMIN_PROCESS),report && report->status==REPORT_PENDING && !app->busy && storage_writes_allowed());
}
static void refresh(GuiApp *app)
{
    int status=(int)SendMessageW(app->filter,CB_GETCURSEL,0,0)-1;
    int no=(int)SendMessageW(app->fault_computer,CB_GETCURSEL,0,0);
    wchar_t message[128];
    app->reports_valid=0; ListView_SetItemCountEx(app->table,0,0); storage_free_reports(&app->reports);
    if (!gui_admin_allowed(app)) { selection(app); return; }
    app->reports_result=fault_query_reports(&app->session,no,status,&app->reports);
    app->reports_valid=app->reports_result==FAULT_OK;
    if (app->reports_valid) {
        ListView_SetItemCountEx(app->table,(int)app->reports.count,0);
        swprintf(message,128,L"已查询 %zu 条报告。选择记录查看完整详情或执行处理。",app->reports.count);
        gui_notice(app,message,0);
    } else { gui_admin_error(app,app->reports_result); }
    ListView_SetItemState(app->table,-1,0,LVIS_SELECTED | LVIS_FOCUSED);
    selection(app);
}
void gui_admin_reports_create(GuiApp *app)
{
    int i;
    wchar_t text[32];
    const wchar_t *columns[]={L"编号",L"报告日期",L"学号",L"电脑",L"状态",L"故障描述"};
    const wchar_t *states[]={L"全部报告",L"待处理",L"已处理"};
    const wchar_t *actions[]={L"确认故障",L"已维修，恢复正常",L"未发现故障，关闭"};
    app->filter=gui_control(app,L"COMBOBOX",L"",WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,GUI_ADMIN_REPORT_STATUS);
    app->fault_computer=gui_control(app,L"COMBOBOX",L"",WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,GUI_ADMIN_REPORT_PC);
    app->action=gui_control(app,L"COMBOBOX",L"",WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,GUI_ADMIN_REPORT_ACTION);
    for (i=0;i<3;++i) {
        SendMessageW(app->filter,CB_ADDSTRING,0,(LPARAM)states[i]);
        SendMessageW(app->action,CB_ADDSTRING,0,(LPARAM)actions[i]);
    }
    SendMessageW(app->fault_computer,CB_ADDSTRING,0,(LPARAM)L"全部电脑");
    for (i=1;i<=COMPUTER_COUNT;++i) {
        swprintf(text,32,L"%02d 号电脑",i); SendMessageW(app->fault_computer,CB_ADDSTRING,0,(LPARAM)text);
    }
    SendMessageW(app->filter,CB_SETCURSEL,0,0); SendMessageW(app->fault_computer,CB_SETCURSEL,0,0);
    SendMessageW(app->action,CB_SETCURSEL,0,0);
    gui_button(app,L"刷新报告",GUI_ADMIN_REFRESH);
    gui_button(app,L"查看详情",GUI_ADMIN_DETAILS);
    gui_button(app,L"处理报告…",GUI_ADMIN_PROCESS);
    app->table=gui_table(app,GUI_ADMIN_TABLE,columns,6);
    refresh(app);
}
void gui_admin_reports_layout(GuiApp *app)
{
    const int weights[]={9,17,18,9,12,35};
    gui_move(app,GUI_ADMIN_REFRESH,app->width-184,106,152,40);
    gui_move(app,GUI_ADMIN_REPORT_STATUS,272,202,160,220);
    gui_move(app,GUI_ADMIN_REPORT_PC,448,202,160,300);
    gui_move(app,GUI_ADMIN_TABLE,272,286,app->width-320,app->height-438);
    gui_move(app,GUI_ADMIN_DETAILS,272,app->height-136,130,40);
    gui_move(app,GUI_ADMIN_REPORT_ACTION,app->width-508,app->height-136,300,230);
    gui_move(app,GUI_ADMIN_PROCESS,app->width-192,app->height-136,144,40);
    gui_table_layout(app,weights,6);
}
void gui_admin_reports_paint(GuiApp *app, HDC dc)
{
    const wchar_t *state=!app->reports_valid ? L"报告加载失败，请检查错误并刷新。" :
        !app->reports.count ? L"当前筛选下暂无故障报告。" : L"选择报告可查看完整详情。处理前将取得最新状态并显示影响。";
    gui_admin_title(app,dc,L"故障报告",L"查询所有学生报告；确认故障、维修恢复与检查后关闭均需二次确认。");
    gui_card(app,dc,256,190,app->width-288,app->height-270,RGB(255,255,255));
    gui_text(app,dc,state,272,248,app->width-320,32,app->body,app->reports_valid ? GUI_MUTED : GUI_RED,0);
}
static void details(GuiApp *app, int id)
{
    FaultReport latest;
    wchar_t text[640], detail[DETAIL_LEN], student[ID_LEN];
    FaultResult result=fault_get_report(&app->session,id,&latest);
    if (result!=FAULT_OK) { gui_admin_error(app,result); return; }
    gui_wide(latest.detail,detail,DETAIL_LEN); gui_wide(latest.student_id,student,ID_LEN);
    swprintf(text,640,L"报告编号：%d\n日期：%08d\n学号：%ls\n电脑：%02d\n状态：%ls\n\n%ls",
        latest.id,latest.date,student,latest.computer_no,latest.status==REPORT_PENDING ? L"待处理" : L"已处理",detail);
    MessageBoxW(app->window,text,L"故障报告详情",MB_OK | MB_ICONINFORMATION);
}
void gui_admin_reports_command(GuiApp *app, int id, int code)
{
    const FaultReport *row;
    if ((id==GUI_ADMIN_REPORT_STATUS || id==GUI_ADMIN_REPORT_PC) && code==CBN_SELCHANGE) { refresh(app); return; }
    if (code!=BN_CLICKED) { return; }
    if (id==GUI_ADMIN_REFRESH) { refresh(app); return; }
    if (id!=GUI_ADMIN_DETAILS && id!=GUI_ADMIN_PROCESS) { return; }
    if (!gui_admin_allowed(app)) { return; }
    row=selected(app);
    if (!row) { gui_notice(app,L"请先选择一条报告。",1); return; }
    if (id==GUI_ADMIN_DETAILS) { details(app,row->id); }
    else {
        int report_id=row->id;
        FaultReport latest;
        ReservationList affected={0};
        FaultAction action=(FaultAction)(SendMessageW(app->action,CB_GETCURSEL,0,0)+1);
        FaultResult result=fault_get_report(&app->session,report_id,&latest);
        wchar_t summary[320];
        app->busy=1; selection(app);
        if (result==FAULT_OK) { result=fault_preview_process(NULL,&app->session,report_id,action,&latest,&affected); }
        if (result==FAULT_OK) {
            swprintf(summary,320,L"报告 %d / %02d 号电脑：%ls。\n将取消 %zu 条尚未开始的有效预约（如下，可滚动）。\n%ls 确认后重新校验报告与预约。",
                latest.id,latest.computer_no,action==FAULT_CONFIRM ? L"确认故障，电脑设为故障" : action==FAULT_REPAIRED ? L"已维修，电脑恢复正常" : L"检查后未发现故障，仅关闭报告",
                affected.count,action==FAULT_REPAIRED ? L"不会恢复已取消预约。" : action==FAULT_DISMISS ? L"不改变电脑状态。" : L"保留已开始预约的历史。");
            if (gui_admin_confirm(app,summary,&affected)) {
                result=fault_process(NULL,&app->session,report_id,action);
                if (result==FAULT_OK) {
                    refresh(app); gui_admin_computers(app); app->seats_valid=0;
                    gui_notice(app,app->reports_valid && app->computers_valid ? L"报告处理成功，报告及电脑状态已刷新。" : L"报告处理已提交成功，但刷新失败。请手动刷新确认最新状态。",!app->reports_valid || !app->computers_valid);
                } else { gui_admin_error(app,result); }
            } else { gui_notice(app,L"已放弃处理，未提交修改。",0); }
        } else { gui_admin_error(app,result); }
        storage_free_reservations(&affected); app->busy=0; selection(app);
    }
}
LRESULT gui_admin_reports_notify(GuiApp *app, NMHDR *header)
{
    if (header->idFrom!=GUI_ADMIN_TABLE) { return 0; }
    if (header->code==LVN_GETDISPINFOW) {
        NMLVDISPINFOW *info=(NMLVDISPINFOW *)header;
        wchar_t text[DETAIL_LEN]={0};
        if (app->reports_valid && info->item.iItem>=0 && (size_t)info->item.iItem<app->reports.count) {
            const FaultReport *row=&app->reports.items[info->item.iItem];
            switch (info->item.iSubItem) {
            case 0: swprintf(text,DETAIL_LEN,L"%d",row->id); break;
            case 1: swprintf(text,DETAIL_LEN,L"%04d-%02d-%02d",row->date/10000,row->date/100%100,row->date%100); break;
            case 2: gui_wide(row->student_id,text,DETAIL_LEN); break;
            case 3: swprintf(text,DETAIL_LEN,L"%02d",row->computer_no); break;
            case 4: wcscpy(text,row->status==REPORT_PENDING ? L"待处理" : L"已处理"); break;
            case 5: gui_wide(row->detail,text,DETAIL_LEN); break;
            }
        }
        gui_table_text(info,text);
    } else if (header->code==LVN_ITEMCHANGED) { selection(app); }
    else if (header->code==NM_DBLCLK) { gui_admin_reports_command(app,GUI_ADMIN_DETAILS,BN_CLICKED); }
    return 0;
}
