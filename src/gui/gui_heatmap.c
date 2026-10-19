#include "gui_heatmap.h"
#include "gui_admin.h"
#include "gui_booking.h"
#include "gui_theme.h"
#include "gui_widgets.h"
#include <wchar.h>

static COLORREF color(int count)
{
    static const COLORREF colors[]={RGB(239,246,255),RGB(191,219,254),RGB(96,165,250),RGB(37,99,235),RGB(30,58,138)};
    int level=heatmap_level(count);
    return level<0 ? GUI_BG : colors[level];
}
static void selected(GuiApp *app)
{
    wchar_t text[180];
    BookingResult time=BOOKING_INVALID_INPUT;
    if(app->heatmap_valid && app->heat_day>=0 && app->heat_slot>=0) {
        const HeatCell *cell=&app->heatmap.cells[app->heat_day][app->heat_slot];
        int date=app->heatmap.dates[app->heat_day];
        swprintf(text,180,L"%08d 周%ls %02d:00–%02d:00，已预约 %d/40，利用率 %.2f%%。",date,
            (const wchar_t *[]){L"一",L"二",L"三",L"四",L"五",L"六",L"日"}[app->heat_day],
            OPEN_HOUR+app->heat_slot,OPEN_HOUR+app->heat_slot+1,cell->count,cell->utilization);
        gui_notice(app,text,0);
        time=booking_check_time(NULL,date,app->heat_slot,1);
    }
    EnableWindow(GetDlgItem(app->window,GUI_HEAT_GO),app->session.role==ROLE_STUDENT && time==BOOKING_OK);
    InvalidateRect(GetDlgItem(app->window,GUI_HEAT_GRID),NULL,FALSE);
}
/* 单一原生自绘网格，63格键盘/鼠标可选，数量不只依靠颜色。 */
static LRESULT CALLBACK grid_proc(HWND window, UINT message, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR data)
{
    GuiApp *app=(GuiApp *)data;
    RECT rect;
    (void)id;
    GetClientRect(window,&rect);
    if(message==WM_LBUTTONDOWN && rect.right>0 && rect.bottom>0) {
        app->heat_day=LOWORD(lp)*7/rect.right; app->heat_slot=HIWORD(lp)*9/rect.bottom;
        if(app->heat_day>6) { app->heat_day=6; } if(app->heat_slot>8) { app->heat_slot=8; }
        SetFocus(window); selected(app); return 0;
    }
    if(message==WM_GETDLGCODE) { return DLGC_WANTARROWS; }
    if(message==WM_KEYDOWN) {
        if(app->heat_day<0) { app->heat_day=0; app->heat_slot=0; }
        else if(wp==VK_LEFT && app->heat_day>0) { --app->heat_day; }
        else if(wp==VK_RIGHT && app->heat_day<6) { ++app->heat_day; }
        else if(wp==VK_UP && app->heat_slot>0) { --app->heat_slot; }
        else if(wp==VK_DOWN && app->heat_slot<8) { ++app->heat_slot; }
        selected(app); return 0;
    }
    if(message==WM_PAINT) {
        PAINTSTRUCT paint;
        HDC dc=BeginPaint(window,&paint);
        int day,slot;
        HGDIOBJ old=SelectObject(dc,app->body);
        for(day=0;day<7;++day) { for(slot=0;slot<9;++slot) {
            RECT cell={day*rect.right/7,slot*rect.bottom/9,(day+1)*rect.right/7,(slot+1)*rect.bottom/9};
            wchar_t text[16];
            int count=app->heatmap_valid ? app->heatmap.cells[day][slot].count : -1;
            gui_fill(dc,cell,count<0 ? GUI_BG : color(count));
            FrameRect(dc,&cell,app->white); SetBkMode(dc,TRANSPARENT);
            SetTextColor(dc,count>24 ? RGB(255,255,255) : GUI_INK);
            if(count>=0) { swprintf(text,16,L"%d",count); } else { wcscpy(text,L"—"); }
            DrawTextW(dc,text,-1,&cell,DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            if(app->heatmap_valid && app->heat_day==day && app->heat_slot==slot) {
                InflateRect(&cell,-2,-2); DrawFocusRect(dc,&cell);
            }
        } }
        SelectObject(dc,old); EndPaint(window,&paint); return 0;
    }
    if(message==WM_NCDESTROY) { RemoveWindowSubclass(window,grid_proc,6); }
    return DefSubclassProc(window,message,wp,lp);
}
void gui_heatmap_refresh(GuiApp *app)
{
    StatsResult result;
    app->heatmap_valid=0; app->heat_day=app->heat_slot=-1;
    result=heatmap_compute(NULL,&app->session,gui_read_date(app->date_picker),&app->heatmap);
    app->heatmap_valid=result==STATS_OK;
    if(result!=STATS_OK) { gui_notice_utf8(app,stats_result_message(result),1); }
    else { gui_notice(app,L"点击或用方向键选择单元格查看数量与利用率。0 表示无有效预约。",0); }
    selected(app); InvalidateRect(app->window,NULL,FALSE);
}
void gui_heatmap_create(GuiApp *app)
{
    HWND grid;
    app->date_picker=gui_date_control(app,GUI_HEAT_DATE);
    gui_button(app,L"刷新热力图",GUI_HEAT_REFRESH);
    if(app->session.role==ROLE_STUDENT) { gui_button(app,L"查询该时段座位",GUI_HEAT_GO); }
    grid=gui_control(app,L"STATIC",L"",WS_TABSTOP | SS_NOTIFY,GUI_HEAT_GRID);
    SetWindowSubclass(grid,grid_proc,6,(DWORD_PTR)app);
    gui_heatmap_refresh(app);
}
void gui_heatmap_layout(GuiApp *app)
{
    gui_move(app,GUI_HEAT_DATE,272,196,190,38);
    gui_move(app,GUI_HEAT_REFRESH,480,196,150,38);
    gui_move(app,GUI_HEAT_GO,app->width-232,196,184,38);
    gui_move(app,GUI_HEAT_GRID,364,288,app->width-412,app->height-430);
}
void gui_heatmap_paint(GuiApp *app, HDC dc)
{
    int day,slot,span=(app->width-412)/7, height=app->height-430;
    wchar_t text[80];
    gui_text(app,dc,L"预约热力图",256,100,app->width-288,44,app->heading,GUI_INK,0);
    gui_text(app,dc,L"周一至周日 · 有效预约数 / 固定容量40 · 预约事实，不代表签到",256,148,app->width-288,28,app->body,GUI_MUTED,0);
    gui_card(app,dc,256,182,app->width-288,app->height-262,RGB(255,255,255));
    for(day=0;day<7;++day) {
        int date=app->heatmap_valid ? app->heatmap.dates[day] : 0;
        swprintf(text,80,L"周%ls\n%02d-%02d",(const wchar_t *[]){L"一",L"二",L"三",L"四",L"五",L"六",L"日"}[day],date/100%100,date%100);
        gui_text(app,dc,text,364+day*span,242,span,42,app->small,GUI_INK,DT_CENTER);
    }
    for(slot=0;slot<9;++slot) {
        swprintf(text,80,L"%02d:00–%02d:00",OPEN_HOUR+slot,OPEN_HOUR+slot+1);
        gui_text(app,dc,text,270,288+slot*height/9,88,height/9,app->small,GUI_MUTED,DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }
    for(day=0;day<5;++day) {
        int x=272+day*(app->width-320)/5;
        RECT rect={gui_scale(app,x),gui_scale(app,app->height-128),gui_scale(app,x+20),gui_scale(app,app->height-110)};
        gui_fill(dc,rect,color(day*8+1));
        swprintf(text,80,day==0 ? L"0–20%%" : L"(%d,%d]%%",day*20,(day+1)*20);
        gui_text(app,dc,text,x+24,app->height-128,(app->width-320)/5-24,28,app->small,GUI_MUTED,0);
    }
}
void gui_heatmap_command(GuiApp *app, int id, int code)
{
    if(app->busy || code!=BN_CLICKED) { return; }
    if(id==GUI_HEAT_REFRESH) { gui_heatmap_refresh(app); }
    else if(id==GUI_HEAT_GO && app->heatmap_valid && app->heat_day>=0 && app->heat_slot>=0) {
        int date=app->heatmap.dates[app->heat_day], slot=app->heat_slot;
        BookingResult result=booking_check_time(NULL,date,slot,1);
        SYSTEMTIME time={0};
        if(!auth_session_has_role(&app->session,ROLE_STUDENT)) { gui_notice(app,L"需要学生登录权限。",1); return; }
        if(result!=BOOKING_OK) { gui_notice_utf8(app,booking_result_message(result),1); return; }
        gui_show_page(app,GUI_SEATS);
        time.wYear=(WORD)(date/10000); time.wMonth=(WORD)(date/100%100); time.wDay=(WORD)(date%100);
        SendMessageW(app->date_picker,DTM_SETSYSTEMTIME,GDT_VALID,(LPARAM)&time);
        SendMessageW(app->slot_picker,CB_SETCURSEL,(WPARAM)slot,0); gui_booking_refresh(app);
    }
}
