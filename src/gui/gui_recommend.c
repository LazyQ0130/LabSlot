#include "gui_recommend.h"
#include "gui_admin.h"
#include "gui_booking.h"
#include "gui_widgets.h"
#include "gui_theme.h"
#include <wchar.h>

void gui_recommend_refresh(GuiApp *app)
{
    BookingResult result;
    int i;
    wchar_t text[180];
    app->recommendation_valid=0; app->recommendations.count=0;
    app->recommend_date=gui_read_date(app->date_picker);
    app->recommend_slot=(int)SendMessageW(app->slot_picker,CB_GETCURSEL,0,0);
    app->recommend_duration=(int)SendMessageW(GetDlgItem(app->window,GUI_SMART_DURATION),CB_GETCURSEL,0,0)+1;
    result=recommend_seats(NULL,&app->session,app->recommend_date,app->recommend_slot,app->recommend_duration,
        (RecommendPreference)SendMessageW(GetDlgItem(app->window,GUI_SMART_PREF),CB_GETCURSEL,0,0),&app->recommendations);
    app->recommendation_valid=result==BOOKING_OK;
    for(i=0;i<3;++i) {
        int available=app->recommendation_valid && (size_t)i<app->recommendations.count;
        if(available) {
            const SeatRecommendation *r=&app->recommendations.seats[i];
            swprintf(text,180,L"%02d 号 · %.2f 分     连续 %d 小时 / 前排 %.2f / 低负载 %.2f",
                r->computer_no,r->score/100.0,r->run_length,r->position/100.0,r->balance/100.0);
        } else { wcscpy(text,L"暂无候选座位"); }
        SetWindowTextW(GetDlgItem(app->window,GUI_SMART_FIRST+i),text);
        EnableWindow(GetDlgItem(app->window,GUI_SMART_FIRST+i),available);
    }
    if(result!=BOOKING_OK) { gui_notice_utf8(app,booking_result_message(result),1); }
    else { gui_notice(app,app->recommendations.count ? L"点击候选前往座位图高亮。推荐没有创建预约；确认时重新校验。" : L"所选区间没有满足条件的电脑，请调整时间。",0); }
    InvalidateRect(app->window,NULL,FALSE);
}
void gui_recommend_create(GuiApp *app)
{
    int i;
    const wchar_t *duration[]={L"1 小时",L"2 小时",L"3 小时"};
    const wchar_t *preferences[]={L"连续可用优先",L"靠前优先",L"负载均衡"};
    HWND length, pref;
    app->date_picker=gui_date_control(app,GUI_SMART_DATE);
    app->slot_picker=gui_slot_control(app,GUI_SMART_SLOT);
    length=gui_control(app,L"COMBOBOX",L"",WS_TABSTOP | CBS_DROPDOWNLIST,GUI_SMART_DURATION);
    pref=gui_control(app,L"COMBOBOX",L"",WS_TABSTOP | CBS_DROPDOWNLIST,GUI_SMART_PREF);
    for(i=0;i<3;++i) {
        SendMessageW(length,CB_ADDSTRING,0,(LPARAM)duration[i]);
        SendMessageW(pref,CB_ADDSTRING,0,(LPARAM)preferences[i]);
        gui_button(app,L"",GUI_SMART_FIRST+i);
    }
    SendMessageW(length,CB_SETCURSEL,0,0); SendMessageW(pref,CB_SETCURSEL,0,0);
    gui_button(app,L"刷新推荐",GUI_SMART_REFRESH);
    gui_recommend_refresh(app);
}
void gui_recommend_layout(GuiApp *app)
{
    int i;
    gui_move(app,GUI_SMART_DATE,280,214,190,38);
    gui_move(app,GUI_SMART_SLOT,486,214,180,250);
    gui_move(app,GUI_SMART_REFRESH,app->width-196,214,140,38);
    gui_move(app,GUI_SMART_DURATION,280,282,190,180);
    gui_move(app,GUI_SMART_PREF,486,282,220,180);
    for(i=0;i<3;++i) { gui_move(app,GUI_SMART_FIRST+i,280,350+i*64,app->width-336,48); }
}
void gui_recommend_paint(GuiApp *app, HDC dc)
{
    int width=app->width-288;
    const int *weights=recommend_weights((RecommendPreference)SendMessageW(GetDlgItem(app->window,GUI_SMART_PREF),CB_GETCURSEL,0,0));
    wchar_t text[160];
    gui_text(app,dc,L"智能座位推荐",256,100,width,44,app->heading,GUI_INK,0);
    gui_text(app,dc,L"真实可用性筛选 · 确定性评分 · 相同分数按编号升序",256,148,width,28,app->body,GUI_MUTED,0);
    gui_card(app,dc,256,182,width,app->height-262,RGB(255,255,255));
    gui_text(app,dc,L"预约日期",280,190,190,22,app->small,GUI_MUTED,0);
    gui_text(app,dc,L"起始时段",486,190,180,22,app->small,GUI_MUTED,0);
    gui_text(app,dc,L"连续时长（推荐不会直接预约）",280,258,320,22,app->small,GUI_MUTED,0);
    if(weights) {
        swprintf(text,160,L"权重：连续 %d%% / 位置 %d%% / 负载 %d%% · 各项0–100分",weights[0],weights[1],weights[2]);
        gui_text(app,dc,text,280,322,width-48,24,app->small,GUI_MUTED,0);
    }
}
void gui_recommend_command(GuiApp *app, int id, int code)
{
    int index=id-GUI_SMART_FIRST;
    if(app->busy) { return; }
    if((id==GUI_SMART_REFRESH && code==BN_CLICKED) ||
       ((id==GUI_SMART_SLOT || id==GUI_SMART_DURATION || id==GUI_SMART_PREF) && code==CBN_SELCHANGE)) {
        gui_recommend_refresh(app);
    } else if(code==BN_CLICKED && index>=0 && index<3 && app->recommendation_valid && (size_t)index<app->recommendations.count) {
        SYSTEMTIME time={0};
        int date=app->recommend_date, slot=app->recommend_slot, no=app->recommendations.seats[index].computer_no, duration=app->recommend_duration;
        if(fault_require_role(&app->session,ROLE_STUDENT)!=FAULT_OK) { gui_notice(app,L"学生会话或维护状态无效，请重新查询。",1); return; }
        gui_show_page(app,GUI_SEATS);
        time.wYear=(WORD)(date/10000); time.wMonth=(WORD)(date/100%100); time.wDay=(WORD)(date%100);
        SendMessageW(app->date_picker,DTM_SETSYSTEMTIME,GDT_VALID,(LPARAM)&time);
        SendMessageW(app->slot_picker,CB_SETCURSEL,(WPARAM)slot,0);
        SendMessageW(GetDlgItem(app->window,GUI_BOOK_DURATION),CB_SETCURSEL,(WPARAM)(duration-1),0);
        gui_booking_refresh(app); gui_booking_command(app,GUI_SEAT_FIRST+no-1);
        if(app->selected==no) { gui_notice(app,L"已高亮推荐座位及连续区间。确认时将重新检查并一次保存全部预约。",0); }
    }
}
