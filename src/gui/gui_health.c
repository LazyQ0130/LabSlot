#include "gui_health.h"
#include "gui_admin.h"
#include "gui_theme.h"
#include "gui_widgets.h"
#include <wchar.h>

static const DeviceHealth *selected(GuiApp *app)
{
    int row=app->table ? ListView_GetNextItem(app->table,-1,LVNI_SELECTED) : -1;
    return app->health_valid && row>=0 && row<COMPUTER_COUNT ? &app->health.devices[row] : NULL;
}
void gui_health_refresh(GuiApp *app)
{
    FaultResult status;
    app->health_valid=0;ListView_SetItemCountEx(app->table,0,0);
    EnableWindow(GetDlgItem(app->window,GUI_HEALTH_REPORTS),FALSE);
    status=health_compute(NULL,&app->session,&app->health);
    if(status==FAULT_OK) {
        app->health_valid=1;ListView_SetItemCountEx(app->table,COMPUTER_COUNT,0);
        gui_notice(app,L"评分依据报告记录和当前状态，仅作维护辅助，不预测未来故障。",0);
    } else { gui_admin_error(app,status); }
    InvalidateRect(app->window,NULL,FALSE);
}
void gui_health_create(GuiApp *app)
{
    const wchar_t *columns[]={L"电脑",L"当前状态",L"健康分",L"评分等级",L"近30天报告",L"未处理报告"};
    gui_button(app,L"刷新分析",GUI_HEALTH_REFRESH);
    gui_button(app,L"查看相关故障报告",GUI_HEALTH_REPORTS);
    app->table=gui_table(app,GUI_HEALTH_TABLE,columns,6);
    gui_health_refresh(app);
}
void gui_health_layout(GuiApp *app)
{
    const int weights[]={10,17,11,18,22,22};
    gui_move(app,GUI_HEALTH_REFRESH,app->width-184,106,152,40);
    gui_move(app,GUI_HEALTH_TABLE,272,292,app->width-320,app->height-500);
    gui_move(app,GUI_HEALTH_REPORTS,app->width-232,app->height-186,184,38);
    gui_table_layout(app,weights,6);
}
void gui_health_paint(GuiApp *app,HDC dc)
{
    int i,width=app->width-288,card=(width-36)/4;
    const DeviceHealth *device=selected(app);
    wchar_t text[420],label[64];
    gui_admin_title(app,dc,L"设备健康分析",L"报告记录与当前状态的启发式评分 · 非硬件检测，不预测未来故障。");
    for(i=0;i<4;++i) {
        int x=256+i*(card+12);
        gui_card(app,dc,x,196,card,76,RGB(255,255,255));
        gui_wide(health_level_message((HealthLevel)i),label,64);
        gui_text(app,dc,label,x+12,203,card-24,24,app->small,GUI_MUTED,0);
        if(app->health_valid) { swprintf(text,420,L"%d 台",app->health.level_counts[i]); } else { wcscpy(text,L"—"); }
        gui_text(app,dc,text,x+12,229,card-24,38,app->number,i==3 ? GUI_RED : GUI_INK,0);
    }
    gui_card(app,dc,256,280,width,app->height-360,RGB(255,255,255));
    if(!app->health_valid) { wcscpy(text,L"分析不可用，未展示旧评分。请检查来源文件、未来报告日期或维护恢复状态。"); }
    else if(!device) { wcscpy(text,L"按健康分升序、同分编号升序显示40台电脑。\n选择设备查看分数构成及历史报告摘要。正常且无报告仅表示暂无故障记录。"); }
    else {
        swprintf(text,420,L"%02d 号：100 − 当前故障 %d − 报告总额度 %d = %d 分（最低0）\n近7天 %d×%d / 第8–30天 %d×%d / 更早 %d×%d / 待处理 %d×%d / 重复修饰 %d\n%ls",
            device->computer_no,device->fault_penalty,device->report_penalty,device->score,
            device->recent_7,HEALTH_RECENT_7_PENALTY,device->days_8_30,HEALTH_DAYS_8_30_PENALTY,
            device->older,HEALTH_OLDER_PENALTY,device->pending,HEALTH_PENDING_PENALTY,device->repeat_penalty,
            device->status==COMPUTER_FAULT ? L"已知故障事实，建议安排处理；这不是预测结论。" :
            device->total_reports==0 ? L"暂无故障记录；不能据此断言硬件完全健康。" : L"历史线索汇总，维修时间轴不完整；可查看相关报告。");
    }
    gui_text(app,dc,text,272,app->height-192,app->width-540,100,app->small,
             !app->health_valid || (device && device->status==COMPUTER_FAULT) ? GUI_RED : GUI_INK,DT_WORDBREAK);
    if(device) {
        const wchar_t *warning=(device->warnings & HEALTH_MANY_PENDING) && (device->warnings & HEALTH_RECENT_REPEAT) ?
            L"未处理较多；近30天重复报障，建议检查" : (device->warnings & HEALTH_MANY_PENDING) ?
            L"未处理报告较多，请关注" : (device->warnings & HEALTH_RECENT_REPEAT) ? L"近30天重复报障，建议检查" : L"未触发额外报告预警";
        gui_text(app,dc,warning,app->width-232,app->height-138,184,48,app->small,GUI_MUTED,DT_WORDBREAK);
    }
}
void gui_health_command(GuiApp *app,int id,int code)
{
    const DeviceHealth *device;
    int no;
    if(app->busy || code!=BN_CLICKED) { return; }
    if(id==GUI_HEALTH_REFRESH) { gui_health_refresh(app); }
    else if(id==GUI_HEALTH_REPORTS) {
        if(!gui_admin_allowed(app)) { return; }
        device=selected(app);if(!device) { return; }no=device->computer_no;
        gui_show_page(app,GUI_ADMIN_REPORTS);
        SendMessageW(app->fault_computer,CB_SETCURSEL,(WPARAM)no,0);
        SendMessageW(app->window,WM_COMMAND,MAKEWPARAM(GUI_ADMIN_REPORT_PC,CBN_SELCHANGE),(LPARAM)app->fault_computer);
    }
}
LRESULT gui_health_notify(GuiApp *app,NMHDR *header)
{
    if(header->idFrom!=GUI_HEALTH_TABLE) { return 0; }
    if(header->code==LVN_GETDISPINFOW) {
        NMLVDISPINFOW *info=(NMLVDISPINFOW *)header;
        wchar_t text[80]=L"";
        if(app->health_valid && info->item.iItem>=0 && info->item.iItem<COMPUTER_COUNT) {
            const DeviceHealth *r=&app->health.devices[info->item.iItem];
            switch(info->item.iSubItem) {
            case 0:swprintf(text,80,L"%02d",r->computer_no);break;
            case 1:wcscpy(text,r->status==COMPUTER_FAULT ? L"故障（已知）" : L"正常");break;
            case 2:swprintf(text,80,L"%d",r->score);break;
            case 3:gui_wide(health_level_message(r->level),text,80);break;
            case 4:swprintf(text,80,L"%d",r->recent_7+r->days_8_30);break;
            case 5:swprintf(text,80,L"%d",r->pending);break;
            }
        }
        gui_table_text(info,text);
    } else if(header->code==LVN_ITEMCHANGED) {
        EnableWindow(GetDlgItem(app->window,GUI_HEALTH_REPORTS),selected(app)!=NULL && !app->busy);
        InvalidateRect(app->window,NULL,FALSE);
    }
    return 0;
}
