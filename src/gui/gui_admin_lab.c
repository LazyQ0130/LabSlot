#include "gui_admin.h"
#include "gui_theme.h"
#include "gui_widgets.h"
#include <wchar.h>

static void selection(GuiApp *app)
{
    int allowed=app->computers_valid && app->selected>0 && storage_writes_allowed() && !app->busy;
    EnableWindow(GetDlgItem(app->window,GUI_ADMIN_MARK),allowed && app->computers[app->selected-1].status==COMPUTER_NORMAL);
    EnableWindow(GetDlgItem(app->window,GUI_ADMIN_RESTORE),allowed && app->computers[app->selected-1].status==COMPUTER_FAULT);
    InvalidateRect(app->window,NULL,FALSE);
}
static void refresh(GuiApp *app)
{
    int i;
    wchar_t text[20];
    app->computers_valid=app->usage_valid=0;
    if (gui_admin_allowed(app)) { gui_admin_computers(app); gui_admin_current(app); }
    for (i=0;i<COMPUTER_COUNT;++i) {
        swprintf(text,20,L"%02d %ls",i+1,!app->computers_valid ? L"未知" : app->computers[i].status==COMPUTER_FAULT ? L"故障" : L"正常");
        SetWindowTextW(GetDlgItem(app->window,GUI_ADMIN_SEAT_FIRST+i),text);
        EnableWindow(GetDlgItem(app->window,GUI_ADMIN_SEAT_FIRST+i),app->computers_valid);
    }
    if (app->computers_valid) {
        if (app->usage_result==FAULT_OK) { gui_notice(app,L"设备状态已更新。选择电脑查看信息及维护操作。",0); }
        else { gui_admin_error(app,app->usage_result); }
    } else if (app->computers_result!=FAULT_OK) { gui_admin_error(app,app->computers_result); }
    selection(app);
}
void gui_admin_lab_create(GuiApp *app)
{
    int i;
    gui_button(app,L"刷新设备",GUI_ADMIN_REFRESH);
    for (i=0;i<COMPUTER_COUNT;++i) { gui_button(app,L"",GUI_ADMIN_SEAT_FIRST+i); }
    gui_button(app,L"标记故障",GUI_ADMIN_MARK);
    gui_button(app,L"恢复正常",GUI_ADMIN_RESTORE);
    refresh(app);
}
void gui_admin_lab_layout(GuiApp *app)
{
    int i, width=app->width-288, grid=width-260, cell=(grid-7*8)/8;
    int row=(app->height-365-4*10)/5;
    gui_move(app,GUI_ADMIN_REFRESH,app->width-184,106,152,40);
    for (i=0;i<COMPUTER_COUNT;++i) {
        gui_move(app,GUI_ADMIN_SEAT_FIRST+i,272+i%8*(cell+8),278+i/8*(row+10),cell,row);
    }
    gui_move(app,GUI_ADMIN_MARK,app->width-248,432,200,40);
    gui_move(app,GUI_ADMIN_RESTORE,app->width-248,484,200,40);
}
void gui_admin_lab_paint(GuiApp *app, HDC dc)
{
    wchar_t text[320];
    size_t i;
    int width=app->width-288;
    gui_admin_title(app,dc,L"电脑维护",L"绿色 正常 · 红色 故障 · 蓝色 已选中。故障确认将取消该电脑尚未开始的有效预约。");
    if (app->computers_valid) { swprintf(text,320,L"总数 %d 台   /   正常 %d 台   /   故障 %d 台",COMPUTER_COUNT,app->normal_count,app->fault_count); }
    else { wcscpy(text,L"电脑数据不可用，请检查错误后刷新。"); }
    gui_text(app,dc,text,256,206,width,28,app->body,GUI_INK,0);
    gui_card(app,dc,256,248,width-244,app->height-328,RGB(255,255,255));
    gui_text(app,dc,L"讲台 / 实验室前方",272,253,width-276,18,app->small,GUI_BLUE,DT_CENTER);
    gui_card(app,dc,app->width-264,248,232,app->height-328,RGB(255,255,255));
    if (!app->selected || !app->computers_valid) { wcscpy(text,L"选择左侧电脑\n\n查看状态与可执行操作"); }
    else {
        swprintf(text,320,L"%02d 号电脑\n当前状态：%ls\n\n",app->selected,app->computers[app->selected-1].status==COMPUTER_FAULT ? L"故障" : L"正常");
        if (app->usage_valid) {
            const AdminUsageRow *found=NULL;
            for (i=0;i<app->usage.count;++i) { if (app->usage.rows[i].computer_no==app->selected) { found=&app->usage.rows[i]; break; } }
            if (found) {
                wchar_t student[ID_LEN], extra[128];
                gui_wide(found->student_id,student,ID_LEN);
                swprintf(extra,128,L"%02d:00–%02d:00 已预约\n学号：%ls\n预约编号：%d",OPEN_HOUR+app->slot,OPEN_HOUR+app->slot+1,student,found->reservation_id);
                wcscat(text,extra);
            } else { wcscat(text,L"当前时段无有效预约"); }
        } else { wcscat(text,app->usage_result==FAULT_OK ? L"当前非开放时段" : L"当前预约情况不可用"); }
    }
    gui_text(app,dc,text,app->width-248,268,200,156,app->body,GUI_INK,DT_WORDBREAK);
}
void gui_admin_lab_command(GuiApp *app, int id)
{
    if (id>=GUI_ADMIN_SEAT_FIRST && id<GUI_ADMIN_SEAT_FIRST+COMPUTER_COUNT) {
        if (app->computers_valid) { app->selected=id-GUI_ADMIN_SEAT_FIRST+1; selection(app); }
    } else if (id==GUI_ADMIN_REFRESH) { refresh(app); }
    else if (id==GUI_ADMIN_MARK || id==GUI_ADMIN_RESTORE) {
        ReservationList affected={0};
        ComputerStatus target=id==GUI_ADMIN_MARK ? COMPUTER_FAULT : COMPUTER_NORMAL;
        FaultResult result;
        wchar_t summary[320];
        if (!gui_admin_allowed(app)) { return; }
        app->busy=1;
        result=fault_preview_change(NULL,&app->session,app->selected,target,&affected);
        if (result==FAULT_OK) {
            swprintf(summary,320,L"%02d 号电脑将%ls。\n将取消 %zu 条尚未开始的有效预约（如下，可滚动）。%ls\n确认时业务层会重新校验；默认选择否。",
                app->selected,target==COMPUTER_FAULT ? L"标记为故障" : L"恢复正常",affected.count,
                target==COMPUTER_NORMAL ? L"已取消的预约不会恢复。" : L"已开始的预约保留历史。");
            if (gui_admin_confirm(app,summary,&affected)) {
                result=fault_change_computer(NULL,&app->session,app->selected,target);
                if (result==FAULT_OK) {
                    refresh(app); app->seats_valid=0;
                    gui_notice(app,app->computers_valid && app->usage_result==FAULT_OK ? L"维护成功，设备及预约情况已刷新。" : L"维护提交成功，但刷新失败，请手动刷新后查看。",!app->computers_valid || app->usage_result!=FAULT_OK);
                } else { gui_admin_error(app,result); }
            } else { gui_notice(app,L"已放弃本次维护，未提交修改。",0); }
        } else { gui_admin_error(app,result); }
        storage_free_reservations(&affected); app->busy=0; selection(app);
    }
}
