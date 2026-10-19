#include "gui_student_stats.h"
#include "gui_widgets.h"
#include "gui_theme.h"
#include <wchar.h>

/* 功能：从唯一业务来源刷新本人统计；参数应用；错误时保留结果码，绘制不使用旧值。 */
void gui_student_stats_refresh(GuiApp *app)
{
    app->stats_result=stats_query_student(NULL,&app->session,&app->student_stats);
}
/* 功能：建立刷新入口并查询；参数应用；无返回。 */
void gui_student_stats_create(GuiApp *app)
{
    gui_button(app,L"刷新统计",GUI_STATS_REFRESH);
    gui_student_stats_refresh(app);
    if (app->stats_result!=STATS_OK) { gui_notice_utf8(app,stats_result_message(app->stats_result),1); }
}
/* 功能：定位刷新按钮；参数应用；无返回。 */
void gui_student_stats_layout(GuiApp *app)
{
    gui_move(app,GUI_STATS_REFRESH,app->width-184,108,152,40);
}
/* 功能：绘制本人身份、五项真实指标与统计口径；参数应用/DC；无返回。 */
void gui_student_stats_paint(GuiApp *app, HDC dc)
{
    int width=app->width-288, card=(width-32)/3, i;
    wchar_t text[192], id[ID_LEN], name[NAME_LEN];
    const wchar_t *labels[]={L"累计完成预约小时",L"有效预约数量",L"已完成预约数量",L"尚未完成数量",L"已取消数量"};
    const int values[]={app->student_stats.completed_hours,app->student_stats.active_count,
        app->student_stats.completed_count,app->student_stats.pending_count,app->student_stats.cancelled_count};
    gui_text(app,dc,L"个人统计",256,100,width-180,44,app->heading,GUI_INK,0);
    gui_text(app,dc,L"根据本人预约记录实时计算，刷新可获取最新结果。",256,146,width,28,app->body,GUI_MUTED,0);
    gui_card(app,dc,256,182,width,94,RGB(255,255,255));
    if (app->stats_result!=STATS_OK) {
        gui_text(app,dc,L"统计暂不可用，请检查错误提示并刷新。",280,212,width-48,44,app->body,GUI_RED,DT_WORDBREAK); return;
    }
    gui_wide(app->student_stats.student_id,id,ID_LEN); gui_wide(app->student_stats.name,name,NAME_LEN);
    swprintf(text,192,L"学号：%ls",id); gui_text(app,dc,text,280,198,width-48,28,app->body,GUI_INK,0);
    swprintf(text,192,L"姓名：%ls",name); gui_text(app,dc,text,280,234,width-48,28,app->body,GUI_INK,0);
    for (i=0;i<5;++i) {
        int x=256+(i%3)*(card+16), y=296+(i/3)*124;
        gui_card(app,dc,x,y,card,108,RGB(255,255,255));
        gui_text(app,dc,labels[i],x+20,y+16,card-40,25,app->body,GUI_MUTED,0);
        swprintf(text,192,L"%d",values[i]);
        gui_text(app,dc,text,x+20,y+50,card-40,44,app->number,i==0 ? GUI_BLUE : GUI_INK,0);
    }
    swprintf(text,192,L"已结束的有效预约计入完成小时，不代表实际上机签到。\n查询时刻：%04d-%02d-%02d %02d:%02d:%02d",
        app->student_stats.as_of.date/10000,app->student_stats.as_of.date/100%100,
        app->student_stats.as_of.date%100,app->student_stats.as_of.hour,
        app->student_stats.as_of.minute,app->student_stats.as_of.second);
    gui_text(app,dc,text,256,548,width,48,app->small,GUI_MUTED,DT_WORDBREAK);
}
