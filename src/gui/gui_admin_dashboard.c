#include "gui_admin.h"
#include "gui_widgets.h"
#include "gui_theme.h"
#include <wchar.h>

void gui_admin_dashboard_refresh(GuiApp *app)
{
    app->computers_valid=app->reports_valid=app->usage_valid=0;
    storage_free_reports(&app->reports);
    if (!gui_admin_allowed(app)) { return; }
    gui_admin_computers(app);
    app->reports_result=fault_query_reports(&app->session,0,REPORT_PENDING,&app->reports);
    app->reports_valid=app->reports_result==FAULT_OK;
    app->pending_reports=app->reports_valid ? app->reports.count : 0;
    storage_free_reports(&app->reports); /* 工作台仅保留数量，不持有报告明细。 */
    gui_admin_current(app);
    if (app->computers_result!=FAULT_OK) { gui_admin_error(app,app->computers_result); }
    else if (app->reports_result!=FAULT_OK) { gui_admin_error(app,app->reports_result); }
    else if (app->usage_result!=FAULT_OK) { gui_admin_error(app,app->usage_result); }
    else { gui_notice(app,L"工作台已刷新，人数按有效预约统计。",0); }
}
void gui_admin_dashboard_create(GuiApp *app)
{
    gui_button(app,L"刷新工作台",GUI_ADMIN_REFRESH);
    gui_button(app,L"电脑维护  →",GUI_ADMIN_QUICK_LAB);
    gui_button(app,L"处理故障报告",GUI_ADMIN_QUICK_REPORTS);
    gui_button(app,L"今日预约统计",GUI_ADMIN_QUICK_STATS);
    gui_admin_dashboard_refresh(app);
}
void gui_admin_dashboard_layout(GuiApp *app)
{
    int card=(app->width-320)/3;
    gui_move(app,GUI_ADMIN_REFRESH,app->width-184,106,152,40);
    gui_move(app,GUI_ADMIN_QUICK_LAB,280,438,card-24,44);
    gui_move(app,GUI_ADMIN_QUICK_REPORTS,296+card,438,card-24,44);
    gui_move(app,GUI_ADMIN_QUICK_STATS,312+card*2,438,card-24,44);
}
void gui_admin_dashboard_paint(GuiApp *app, HDC dc)
{
    const wchar_t *labels[]={L"电脑总数",L"正常电脑",L"故障电脑",L"当前时段预约人数",L"待处理故障报告"};
    int i, width=app->width-288, card=(width-32)/3;
    wchar_t values[5][64];
    gui_admin_title(app,dc,L"管理工作台",L"设备、预约与报告概览 · 当前时段人数仅表示预约，不代表实际签到。");
    for (i=0;i<3;++i) {
        if (app->computers_valid) { swprintf(values[i],64,L"%d",i==0 ? COMPUTER_COUNT : i==1 ? app->normal_count : app->fault_count); }
        else { wcscpy(values[i],L"不可用"); }
    }
    if (app->usage_valid) { swprintf(values[3],64,L"%zu",app->usage.count); }
    else { wcscpy(values[3],app->usage_result==FAULT_OK && !app->current_open ? L"非开放时段" : L"不可用"); }
    if (app->reports_valid) { swprintf(values[4],64,L"%zu",app->pending_reports); }
    else { wcscpy(values[4],L"不可用"); }
    for (i=0;i<5;++i) {
        int x=256+(i<3 ? i : i-3)*(card+16), y=i<3 ? 206 : 324;
        int w=i<3 ? card : (width-16)/2;
        if (i==4) { x=256+(width+16)/2; }
        gui_card(app,dc,x,y,w,102,RGB(255,255,255));
        gui_text(app,dc,labels[i],x+20,y+14,w-40,24,app->body,GUI_MUTED,0);
        gui_text(app,dc,values[i],x+20,y+45,w-40,44,app->number,i==2 && app->fault_count ? GUI_RED : GUI_INK,0);
    }
    gui_text(app,dc,L"维护操作会先展示受影响预约；所有提交均由业务层重新验证。",256,505,width,42,app->body,GUI_MUTED,DT_WORDBREAK);
}
