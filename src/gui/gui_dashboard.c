#include "gui_dashboard.h"
#include "gui_widgets.h"
#include "gui_theme.h"
#include "gui_student_stats.h"
#include <wchar.h>

/* 功能：建立当前角色导航；参数应用；无返回。 */
void gui_shell_create(GuiApp *app)
{
    gui_button(app,L"▦  工作台",GUI_HOME);
    gui_button(app,app->session.role==ROLE_STUDENT ? L"▤  座位预约" : L"▤  座位概览",GUI_BOOKING);
    if (app->session.role==ROLE_STUDENT) {
        gui_button(app,L"▤  我的预约",GUI_NAV_RESERVATIONS);
        gui_button(app,L"◇  故障报告",GUI_NAV_FAULT);
        gui_button(app,L"▥  个人统计",GUI_NAV_STATS);
    }
    gui_button(app,L"退出登录",GUI_LOGOUT);
}
/* 功能：定位侧栏导航；参数应用；无返回。 */
void gui_shell_layout(GuiApp *app)
{
    gui_move(app,GUI_HOME,16,110,192,44);
    gui_move(app,GUI_BOOKING,16,164,192,44);
    gui_move(app,GUI_LOGOUT,16,app->height-70,192,42);
    gui_move(app,GUI_NAV_RESERVATIONS,16,218,192,44);
    gui_move(app,GUI_NAV_FAULT,16,272,192,44);
    gui_move(app,GUI_NAV_STATS,16,326,192,44);
}
/* 功能：绘制产品头部、身份与角色功能说明；参数应用/DC；无返回。 */
void gui_shell_paint(GuiApp *app, HDC dc)
{
    RECT header={0,0,gui_scale(app,app->width),gui_scale(app,80)};
    RECT sidebar={0,gui_scale(app,80),gui_scale(app,224),gui_scale(app,app->height)};
    wchar_t identity[96], user[ID_LEN];
    gui_fill(dc,header,RGB(255,255,255)); gui_fill(dc,sidebar,RGB(255,255,255));
    gui_text(app,dc,L"LabSlot",24,19,175,44,app->heading,GUI_BLUE,0);
    gui_text(app,dc,L"智能实验室预约系统",256,29,300,28,app->body,GUI_MUTED,0);
    gui_wide(app->session.user_id,user,ID_LEN);
    swprintf(identity,96,L"%ls  ·  %ls",app->session.role==ROLE_STUDENT ? L"学生" : L"管理员",user);
    gui_text(app,dc,identity,app->width-360,29,328,28,app->body,GUI_INK,DT_RIGHT);
    if (app->session.role==ROLE_ADMIN) {
        gui_text(app,dc,L"后续图形功能",24,250,176,24,app->small,GUI_MUTED,0);
        gui_text(app,dc,
        L"设备维护 · 未开放\n\n学生查询 · 未开放\n\n故障处理 · 未开放\n\n预约统计 · 未开放",
        24,290,176,240,app->body,GUI_MUTED,DT_WORDBREAK);
        gui_text(app,dc,L"以上功能可在控制台使用",24,app->height-112,180,32,app->small,GUI_MUTED,DT_WORDBREAK);
    } else {
        gui_text(app,dc,L"学生服务\n数据按操作实时查询",24,app->height-128,180,50,app->small,GUI_MUTED,DT_WORDBREAK);
    }
}
/* 功能：获取真实本人统计并建立快捷入口；参数应用；无返回。 */
void gui_dashboard_create(GuiApp *app)
{
    app->stats_result=app->session.role==ROLE_STUDENT ?
        stats_query_student(NULL,&app->session,&app->student_stats) : STATS_UNAUTHORIZED;
    gui_button(app,app->session.role==ROLE_STUDENT ? L"选择座位  →" : L"查看座位  →",GUI_QUICK);
    if (app->session.role==ROLE_STUDENT) {
        gui_button(app,L"我的预约",GUI_QUICK_RESERVATIONS);
        gui_button(app,L"报告故障",GUI_QUICK_FAULT);
        gui_button(app,L"刷新工作台",GUI_HOME_REFRESH);
    }
}
/* 功能：定位工作台快捷操作；参数应用；无返回。 */
void gui_dashboard_layout(GuiApp *app)
{
    gui_move(app,GUI_QUICK,284,465,200,46);
    gui_move(app,GUI_QUICK_RESERVATIONS,500,465,160,46);
    gui_move(app,GUI_QUICK_FAULT,676,465,160,46);
    gui_move(app,GUI_HOME_REFRESH,app->width-184,110,152,40);
}
/* 功能：绘制基本容量及可用的真实统计；参数应用/DC；无返回。 */
void gui_dashboard_paint(GuiApp *app, HDC dc)
{
    SYSTEMTIME now;
    wchar_t text[160], value[48];
    int x=256, width=app->width-288, gap=16, card=(width-2*gap)/3, i;
    const wchar_t *labels[]={L"实验室电脑",L"每日开放时段",L"预约时间单位"};
    const wchar_t *values[]={L"40 台",L"9 个",L"1 小时"};
    GetLocalTime(&now);
    gui_text(app,dc,L"欢迎使用实验室预约系统",x,110,width-180,46,app->heading,GUI_INK,0);
    swprintf(text,160,L"%04u 年 %02u 月 %02u 日  ·  开放时间 08:00–17:00",
             now.wYear,now.wMonth,now.wDay);
    gui_text(app,dc,text,x,164,width,30,app->body,GUI_MUTED,0);
    for (i=0;i<3;++i) {
        gui_card(app,dc,x+i*(card+gap),216,card,130,RGB(255,255,255));
        gui_text(app,dc,labels[i],x+i*(card+gap)+24,238,card-48,28,app->body,GUI_MUTED,0);
        gui_text(app,dc,values[i],x+i*(card+gap)+24,278,card-48,48,app->number,GUI_INK,0);
    }
    gui_card(app,dc,x,374,width,160,RGB(239,246,255));
    gui_text(app,dc,L"下一次实验，从选择座位开始",x+28,394,width-56,36,app->heading,GUI_INK,0);
    gui_text(app,dc,app->session.role==ROLE_STUDENT ? L"按日期与时段查看实时状态，选择空闲电脑并确认预约。" :
             L"查看真实占用与故障状态。管理员账号仅可查看座位。",x+28,436,width-56,25,app->body,GUI_MUTED,0);
    if (app->session.role==ROLE_STUDENT && app->stats_result==STATS_OK && app->height>=760) {
        const int numbers[]={app->student_stats.active_count,app->student_stats.pending_count,
                             app->student_stats.completed_hours};
        const wchar_t *titles[]={L"我的有效预约",L"尚未完成预约",L"累计完成预约小时"};
        for (i=0;i<3;++i) {
            gui_card(app,dc,x+i*(card+gap),562,card,120,RGB(255,255,255));
            gui_text(app,dc,titles[i],x+i*(card+gap)+24,582,card-48,26,app->body,GUI_MUTED,0);
            swprintf(value,48,L"%d",numbers[i]);
            gui_text(app,dc,value,x+i*(card+gap)+24,618,card-48,44,app->number,GUI_INK,0);
        }
        gui_text(app,dc,L"按预约事实实时计算，完成小时不代表实际签到。",x,696,width,25,app->small,GUI_MUTED,0);
    } else if (app->session.role==ROLE_STUDENT && app->stats_result!=STATS_OK) {
        wchar_t error[160];
        gui_wide(stats_result_message(app->stats_result),error,160);
        gui_text(app,dc,error,x,560,width,48,app->body,GUI_RED,DT_WORDBREAK);
    }
}
