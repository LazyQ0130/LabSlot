#include "gui_booking.h"
#include "gui_widgets.h"
#include "gui_theme.h"
#include "storage.h"
#include "gui_student_stats.h"
#include <wchar.h>

/* 功能：创建日期、时段和 40 个原生座位控件；参数应用；无返回。 */
void gui_booking_create(GuiApp *app)
{
    SYSTEMTIME now, range[2]={{0},{0}};
    int i;
    wchar_t text[64];
    app->date_picker=gui_control(app,DATETIMEPICK_CLASSW,L"",WS_TABSTOP | DTS_SHORTDATEFORMAT,GUI_DATE);
    SendMessageW(app->date_picker,DTM_SETFORMATW,0,(LPARAM)L"yyyy-MM-dd");
    range[0].wYear=1000; range[0].wMonth=1; range[0].wDay=1;
    range[1].wYear=9999; range[1].wMonth=12; range[1].wDay=31;
    SendMessageW(app->date_picker,DTM_SETRANGE,GDTR_MIN | GDTR_MAX,(LPARAM)range);
    GetLocalTime(&now);
    SendMessageW(app->date_picker,DTM_SETSYSTEMTIME,GDT_VALID,(LPARAM)&now);
    app->slot_picker=gui_control(app,L"COMBOBOX",L"",WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,GUI_SLOT);
    for (i=0;i<SLOT_COUNT;++i) {
        swprintf(text,64,L"%02d:00–%02d:00",OPEN_HOUR+i,OPEN_HOUR+i+1);
        SendMessageW(app->slot_picker,CB_ADDSTRING,0,(LPARAM)text);
    }
    app->slot=now.wHour<OPEN_HOUR ? 0 : now.wHour>=CLOSE_HOUR ? SLOT_COUNT-1 : now.wHour-OPEN_HOUR;
    SendMessageW(app->slot_picker,CB_SETCURSEL,(WPARAM)app->slot,0);
    gui_button(app,L"刷新座位",GUI_REFRESH);
    for (i=0;i<COMPUTER_COUNT;++i) { gui_button(app,L"",GUI_SEAT_FIRST+i); }
    if (app->session.role==ROLE_STUDENT) { gui_button(app,L"确认预约",GUI_RESERVE); gui_button(app,L"智能推荐",GUI_SMART_OPEN); gui_button(app,L"本周预约热度",GUI_HEAT_OPEN); }
    gui_booking_refresh(app);
}
/* 功能：按 5×8 和可用空间布局座位；参数应用；无返回。 */
void gui_booking_layout(GuiApp *app)
{
    int i, width=app->width-336, gap=8, cell=(width-7*gap)/8;
    int row_gap=6, row=(app->height-436-4*row_gap)/5;
    gui_move(app,GUI_DATE,280,193,190,38);
    gui_move(app,GUI_SLOT,490,193,180,300);
    gui_move(app,GUI_REFRESH,690,193,130,38);
    for (i=0;i<COMPUTER_COUNT;++i) {
        gui_move(app,GUI_SEAT_FIRST+i,280+(i%8)*(cell+gap),292+(i/8)*(row+row_gap),cell,row);
    }
    gui_move(app,GUI_HEAT_OPEN,app->width-184,106,152,38);
    gui_move(app,GUI_SMART_OPEN,app->width-352,106,152,38);
    gui_move(app,GUI_RESERVE,app->width-204,app->height-132,148,42);
}
/* 功能：同步座位文本、可用性及预约按钮；参数应用；无返回。 */
static void update_seats(GuiApp *app)
{
    int i;
    wchar_t text[64];
    for (i=0;i<COMPUTER_COUNT;++i) {
        char symbol=app->seats_valid ? app->seats.symbols[i] : '?';
        swprintf(text,64,L"%02d  %ls",i+1,symbol=='*' ? L"空闲" : symbol=='#' ? L"预约" : symbol=='!' ? L"故障" : L"未知");
        SetWindowTextW(GetDlgItem(app->window,GUI_SEAT_FIRST+i),text);
        EnableWindow(GetDlgItem(app->window,GUI_SEAT_FIRST+i),app->session.role==ROLE_STUDENT && symbol=='*');
    }
    EnableWindow(GetDlgItem(app->window,GUI_RESERVE),app->selected>0 && app->seats_valid && !app->busy);
    InvalidateRect(app->window,NULL,FALSE);
}
/* 功能：清除旧选择并从业务接口查询；参数应用；失败禁用座位，无返回。 */
void gui_booking_refresh(GuiApp *app)
{
    SYSTEMTIME time={0};
    BookingResult result;
    app->selected=0; app->seats_valid=0;
    if (!storage_writes_allowed()) {
        gui_notice(app,L"维护恢复未完成，座位查询和预约已暂停。请检查数据并重新启动以恢复。",1);
        update_seats(app); return;
    }
    if (SendMessageW(app->date_picker,DTM_GETSYSTEMTIME,0,(LPARAM)&time)!=GDT_VALID) {
        gui_notice(app,L"请选择有效日期。",1); update_seats(app); return;
    }
    app->date=time.wYear*10000+time.wMonth*100+time.wDay;
    app->slot=(int)SendMessageW(app->slot_picker,CB_GETCURSEL,0,0);
    result=booking_get_seats(app->date,app->slot,&app->seats);
    if (result==BOOKING_OK) {
        app->seats_valid=1; gui_notice(app,L"座位状态已更新。切换日期或时段后会清除旧选择。",0);
    } else { gui_notice_utf8(app,booking_result_message(result),1); }
    update_seats(app);
}
/* 功能：绘制真实数量、图例与选择说明；参数应用/DC；无返回。 */
void gui_booking_paint(GuiApp *app, HDC dc)
{
    wchar_t text[160];
    int width=app->width-288;
    gui_text(app,dc,app->session.role==ROLE_STUDENT ? L"座位预约" : L"座位概览",256,100,width,44,app->heading,GUI_INK,0);
    gui_text(app,dc,L"选择日期与时段，查看实验室的真实座位状态。",256,146,width,26,app->body,GUI_MUTED,0);
    gui_card(app,dc,256,178,width,app->height-258,RGB(255,255,255));
    if (app->seats_valid) {
        swprintf(text,160,L"共 40 台   ·   空闲 %d   ·   已预约 %d   ·   故障 %d",
                 app->seats.free_count,app->seats.booked_count,app->seats.fault_count);
    } else { wcscpy(text,L"座位状态不可用：未展示旧数据，请检查错误提示并刷新。"); }
    gui_text(app,dc,text,280,241,width-48,24,app->body,app->seats_valid ? GUI_INK : GUI_RED,0);
    gui_card(app,dc,280,268,width-48,20,RGB(239,246,255));
    gui_text(app,dc,L"讲台 / 实验室前方 · 5 排 × 8 列",280,270,width-48,16,app->small,GUI_BLUE,DT_CENTER);
    if (app->selected) { swprintf(text,160,L"已选择 %02d 号电脑  ·  蓝色高亮",app->selected); }
    else { wcscpy(text,app->session.role==ROLE_STUDENT ? L"绿色 空闲   /   灰色 已预约   /   红色 故障" : L"只读视图 · 管理员不可预约"); }
    gui_text(app,dc,text,280,app->height-132,width-224,40,app->body,GUI_MUTED,DT_WORDBREAK);
}
/* 确认前预检、确认后业务重新读取并校验；仅保存成功才显示正式编号。 */
/* 功能：预检、确认、业务保存、重新查询；参数应用；失败保留具体提示，无返回。 */
static void reserve(GuiApp *app)
{
    BookingResult result;
    Reservation created={0};
    int no=app->selected, date=app->date, slot=app->slot;
    wchar_t prompt[256], user[ID_LEN];
    if (app->busy || !auth_session_has_role(&app->session,ROLE_STUDENT) || !app->seats_valid || no<1) { return; }
    app->busy=1; EnableWindow(GetDlgItem(app->window,GUI_RESERVE),FALSE);
    if (!storage_writes_allowed()) {
        gui_notice(app,L"维护恢复未完成，不能预约。请检查数据并重新启动。",1); goto done;
    }
    result=booking_check_create(NULL,&app->session,date,slot,no);
    if (result!=BOOKING_OK) { gui_booking_refresh(app); gui_notice_utf8(app,booking_result_message(result),1); goto done; }
    gui_wide(app->session.user_id,user,ID_LEN);
    swprintf(prompt,256,L"学号：%ls\n日期：%04d-%02d-%02d\n时段：%02d:00–%02d:00\n电脑：%02d\n\n确认创建这一小时的预约？",
             user,date/10000,date/100%100,date%100,OPEN_HOUR+slot,OPEN_HOUR+slot+1,no);
    if (MessageBoxW(app->window,prompt,L"确认预约",MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2)!=IDYES) { goto done; }
    result=booking_create(NULL,&app->session,date,slot,no,&created);
    gui_booking_refresh(app);
    if (result==BOOKING_OK) {
        gui_student_stats_refresh(app);
        swprintf(prompt,256,L"预约成功，编号 %d。座位状态已重新查询。",created.id);
        gui_notice(app,prompt,0);
    } else { gui_notice_utf8(app,booking_result_message(result),1); }
done:
    app->busy=0; update_seats(app);
}
/* 功能：处理座位刷新、选择和预约；参数应用/控件编号；无返回。 */
void gui_booking_command(GuiApp *app, int id)
{
    int index=id-GUI_SEAT_FIRST;
    if (app->busy) { return; }
    if (id==GUI_REFRESH) { gui_booking_refresh(app); }
    else if (id==GUI_RESERVE) { reserve(app); }
    else if (index>=0 && index<COMPUTER_COUNT && app->seats_valid &&
             auth_session_has_role(&app->session,ROLE_STUDENT) && app->seats.symbols[index]=='*') {
        app->selected=index+1; update_seats(app);
    }
}
