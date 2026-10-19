#include "gui_admin.h"
#include "gui_widgets.h"
#include "gui_theme.h"
#include <wchar.h>

static void stats_error(GuiApp *app)
{
    if (app->stats_result==STATS_RECOVERY_REQUIRED) { gui_admin_error(app,FAULT_RECOVERY_REQUIRED); }
    else { gui_notice_utf8(app,stats_result_message(app->stats_result),1); }
}
void gui_admin_stats_refresh(GuiApp *app)
{
    app->admin_stats_valid=0;
    app->stats_mode=(int)SendMessageW(app->filter,CB_GETCURSEL,0,0);
    app->date=gui_read_date(app->date_picker);
    EnableWindow(GetDlgItem(app->window,GUI_ADMIN_EXPORT),FALSE);
    if (!gui_admin_allowed(app)) { return; }
    app->stats_result=app->stats_mode==0 ? stats_compute_daily(NULL,&app->session,app->date,&app->daily) :
        stats_compute_week(NULL,&app->session,app->date,&app->weekly);
    app->admin_stats_valid=app->stats_result==STATS_OK;
    if (app->admin_stats_valid) {
        EnableWindow(GetDlgItem(app->window,GUI_ADMIN_EXPORT),app->stats_mode==0 && storage_writes_allowed());
        gui_notice(app,L"统计按有效预约事实计算，已结束预约小时不代表实际上机签到。",0);
    } else { stats_error(app); }
    InvalidateRect(app->window,NULL,FALSE);
}
void gui_admin_stats_create(GuiApp *app)
{
    app->filter=gui_control(app,L"COMBOBOX",L"",WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,GUI_ADMIN_STATS_MODE);
    SendMessageW(app->filter,CB_ADDSTRING,0,(LPARAM)L"每日统计");
    SendMessageW(app->filter,CB_ADDSTRING,0,(LPARAM)L"每周统计");
    SendMessageW(app->filter,CB_SETCURSEL,0,0);
    app->date_picker=gui_date_control(app,GUI_ADMIN_STATS_DATE);
    gui_button(app,L"刷新统计",GUI_ADMIN_REFRESH);
    gui_button(app,L"预约热力图",GUI_HEAT_OPEN);
    gui_button(app,L"导出每日统计",GUI_ADMIN_EXPORT);
    gui_admin_stats_refresh(app);
}
void gui_admin_stats_layout(GuiApp *app)
{
    gui_move(app,GUI_HEAT_OPEN,app->width-352,106,152,40);
    gui_move(app,GUI_ADMIN_REFRESH,app->width-184,106,152,40);
    gui_move(app,GUI_ADMIN_STATS_MODE,256,198,160,220);
    gui_move(app,GUI_ADMIN_STATS_DATE,432,198,176,36);
    gui_move(app,GUI_ADMIN_EXPORT,app->width-212,198,180,36);
}
/* 绘制与测试共用此投影访问器，没有聚合或文件读取。 */
int gui_admin_chart_value(const GuiApp *app, int index)
{
    if (!app->admin_stats_valid || index<0 || index>=(app->stats_mode==0 ? SLOT_COUNT : 7)) { return 0; }
    return app->stats_mode==0 ? app->daily.counts.slot_counts[index] : app->weekly.days[index].counts.reserved_hours;
}
static void chart(GuiApp *app, HDC dc)
{
    int i, width=app->width-288, left=312, top=432, bottom=app->height-132;
    int count=app->stats_mode==0 ? SLOT_COUNT : 7, maximum=app->stats_mode==0 ? COMPUTER_COUNT : DAILY_CAPACITY;
    int span=(app->width-360)/count;
    HPEN pen=CreatePen(PS_SOLID,1,GUI_LINE);
    HGDIOBJ old=SelectObject(dc,pen);
    wchar_t text[64];
    gui_card(app,dc,256,360,width,app->height-444,RGB(255,255,255));
    gui_text(app,dc,app->stats_mode==0 ?
        (app->daily.counts.reservation_count ? L"各时段有效预约数 / 每时段容量 40" : L"当日暂无有效预约 / 每时段容量 40") :
        (app->weekly.reservation_count ? L"每日预约小时 / 每日容量 360" : L"本周暂无有效预约 / 每日容量 360"),
        280,376,width-48,24,app->body,GUI_INK,0);
    for (i=0;i<=4;++i) {
        int y=bottom-(bottom-top)*i/4;
        MoveToEx(dc,gui_scale(app,left),gui_scale(app,y),NULL);
        LineTo(dc,gui_scale(app,app->width-56),gui_scale(app,y));
        swprintf(text,64,L"%d",maximum*i/4);
        gui_text(app,dc,text,270,y-8,34,20,app->small,GUI_MUTED,DT_RIGHT);
    }
    SelectObject(dc,old); DeleteObject(pen);
    for (i=0;i<count;++i) {
        int value=gui_admin_chart_value(app,i), height=MulDiv(bottom-top,value,maximum), x=left+i*span;
        RECT bar={gui_scale(app,x+span/4),gui_scale(app,bottom-height),gui_scale(app,x+span*3/4),gui_scale(app,bottom)};
        gui_fill(dc,bar,GUI_BLUE);
        swprintf(text,64,L"%d",value);
        gui_text(app,dc,text,x, bottom-height-20,span,20,app->small,GUI_BLUE,DT_CENTER);
        if (app->stats_mode==0) { swprintf(text,64,L"%02d:00–\n%02d:00",OPEN_HOUR+i,OPEN_HOUR+i+1); }
        else {
            int date=app->weekly.days[i].counts.date;
            swprintf(text,64,L"%02d-%02d\n周%ls",date/100%100,date%100,(const wchar_t *[]){L"一",L"二",L"三",L"四",L"五",L"六",L"日"}[i]);
        }
        gui_text(app,dc,text,x,bottom+8,span,38,app->small,GUI_MUTED,DT_CENTER);
    }
}
void gui_admin_stats_paint(GuiApp *app, HDC dc)
{
    wchar_t text[180], values[4][48];
    int i, width=app->width-288, card=(width-36)/4;
    const wchar_t *daily_labels[]={L"有效预约数",L"去重学生人数",L"预约小时数",L"预约利用率"};
    const wchar_t *week_labels[]={L"整周预约小时",L"整周去重学生",L"日均预约小时",L"整周利用率"};
    gui_admin_title(app,dc,L"数据统计",L"实时日报与完整周报 · 容量为每日 360 / 每周 2520 座位小时。");
    if (!app->admin_stats_valid) {
        gui_text(app,dc,L"统计不可用，请查看错误并刷新。未展示旧统计值。",256,268,width,56,app->body,GUI_RED,DT_WORDBREAK); return;
    }
    if (app->stats_mode==0) {
        swprintf(text,180,L"%04d-%02d-%02d 每日预约统计",app->date/10000,app->date/100%100,app->date%100);
        swprintf(values[0],48,L"%d",app->daily.counts.reservation_count);
        swprintf(values[1],48,L"%d",app->daily.counts.student_count);
        swprintf(values[2],48,L"%d",app->daily.counts.reserved_hours);
        swprintf(values[3],48,L"%.2f%%",app->daily.utilization);
    } else {
        swprintf(text,180,L"周一 %08d — 周日 %08d",app->weekly.start_date,app->weekly.end_date);
        swprintf(values[0],48,L"%d",app->weekly.reservation_count);
        swprintf(values[1],48,L"%d",app->weekly.student_count);
        swprintf(values[2],48,L"%.2f",app->weekly.average_daily_hours);
        swprintf(values[3],48,L"%.2f%%",app->weekly.utilization);
    }
    gui_text(app,dc,text,256,246,width,24,app->body,GUI_INK,0);
    for (i=0;i<4;++i) {
        int x=256+i*(card+12);
        gui_card(app,dc,x,278,card,72,RGB(255,255,255));
        gui_text(app,dc,app->stats_mode==0 ? daily_labels[i] : week_labels[i],x+12,285,card-24,23,app->small,GUI_MUTED,0);
        gui_text(app,dc,values[i],x+12,309,card-24,38,app->number,GUI_INK,0);
    }
    chart(app,dc);
}
void gui_admin_stats_export(GuiApp *app)
{
    wchar_t summary[320];
    if (!gui_admin_allowed(app)) { return; }
    if (app->stats_mode!=0 || !app->admin_stats_valid) { gui_notice(app,L"请先成功查询每日统计，再导出。",1); return; }
    swprintf(summary,320,L"导出日期：%08d\n当前显示：有效预约 %d 条，去重学生 %d 人，预约 %d 小时，利用率 %.2f%%。\n确认后重新计算并更新当日快照；其他日期由业务层保留。",
        app->date,app->daily.counts.reservation_count,app->daily.counts.student_count,app->daily.counts.reserved_hours,app->daily.utilization);
    app->busy=1;
    if (gui_admin_confirm(app,summary,NULL)) {
        app->stats_result=stats_export_daily(NULL,&app->session,app->date,&app->daily);
        if (app->stats_result==STATS_OK) {
            gui_notice(app,L"已更新当日统计快照。后续预约变化可能使快照过时，可重新导出。",0);
        } else { stats_error(app); }
    } else { gui_notice(app,L"已放弃导出，未更新统计快照。",0); }
    app->busy=0;
}
