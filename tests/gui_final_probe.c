/* P5.4: actual Windows controls, real local clock, no production demo hooks. */
#include "gui_app.h"
#include "gui_widgets.h"
#include "gui_admin.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

static int total,failed;
static const char *pause_at="";
static int tomorrow;
static const wchar_t *password=L"FinalTest9!"; /* 合成测试凭据，从不输出。 */
#define CHECK(label,expr) do { ++total; if(expr) printf("PASS %s\n",label); else { ++failed; printf("FAIL %s line %d\n",label,__LINE__); } fflush(stdout); } while(0)
static void click(GuiApp *app,int id) { SendMessageW(app->window,WM_COMMAND,MAKEWPARAM(id,BN_CLICKED),(LPARAM)GetDlgItem(app->window,id)); }
static void pump(GuiApp *app)
{
    MSG message;
    UpdateWindow(app->window);
    while(PeekMessageW(&message,NULL,0,0,PM_REMOVE)) {
        if(message.message!=WM_QUIT && !gui_translate_message(app,&message)) { TranslateMessage(&message); DispatchMessageW(&message); }
    }
}
static void pause_view(GuiApp *app,const char *stage)
{
    MSG message;
    if(strcmp(pause_at,stage)) return;
    printf("VIEW %s actual DPI %u client %d x %d\n",stage,app->dpi,app->width,app->height); fflush(stdout);
    while(GetMessageW(&message,NULL,0,0)>0) {
        if(!gui_translate_message(app,&message)) { TranslateMessage(&message); DispatchMessageW(&message); }
    }
    exit(0);
}
static void combo(GuiApp *app,int id,int index)
{
    SendMessageW(GetDlgItem(app->window,id),CB_SETCURSEL,(WPARAM)index,0);
    SendMessageW(app->window,WM_COMMAND,MAKEWPARAM(id,CBN_SELCHANGE),(LPARAM)GetDlgItem(app->window,id));
}
static void set_date(GuiApp *app,int id,int date)
{
    SYSTEMTIME time={0};
    time.wYear=(WORD)(date/10000); time.wMonth=(WORD)(date/100%100); time.wDay=(WORD)(date%100);
    SendMessageW(GetDlgItem(app->window,id),DTM_SETSYSTEMTIME,GDT_VALID,(LPARAM)&time);
}
typedef struct { HWND owner,dialog; DWORD thread; int response,rows,default_no; } Answer;
static BOOL CALLBACK find_modal(HWND window,LPARAM parameter)
{
    Answer *answer=(Answer *)parameter;
    wchar_t name[64];
    GetClassNameW(window,name,64);
    if(IsWindowVisible(window) && GetWindow(window,GW_OWNER)==answer->owner &&
       (!wcscmp(name,L"#32770") || !wcscmp(name,L"LabSlotAdminConfirm"))) { answer->dialog=window; return FALSE; }
    return TRUE;
}
static DWORD WINAPI answer_modal(void *parameter)
{
    Answer *answer=parameter;
    int i;
    for(i=0;i<600;++i) {
        EnumThreadWindows(answer->thread,find_modal,(LPARAM)answer);
        if(answer->dialog) {
            GUITHREADINFO info={0};
            HWND table;
            Sleep(30); info.cbSize=sizeof info; GetGUIThreadInfo(answer->thread,&info);
            if(!IsChild(answer->dialog,info.hwndFocus)) { answer->dialog=NULL; Sleep(10); continue; }
            table=GetDlgItem(answer->dialog,GUI_ADMIN_PREVIEW_TABLE);
            answer->rows=table ? ListView_GetItemCount(table) : -1;
            answer->default_no=GetDlgCtrlID(info.hwndFocus)==(table ? GUI_ADMIN_NO : IDNO);
            PostMessageW(answer->dialog,WM_COMMAND,(WPARAM)(table ? (answer->response==IDYES ? GUI_ADMIN_YES : GUI_ADMIN_NO) : answer->response),0);
            return 0;
        }
        Sleep(10);
    }
    return 1;
}
static Answer modal(GuiApp *app,int id,int response)
{
    Answer answer={app->window,NULL,GetCurrentThreadId(),response,0,0};
    HANDLE thread=CreateThread(NULL,0,answer_modal,&answer,0,NULL);
    DWORD result=1;
    if(!thread) { CHECK("confirmation worker available",0); return answer; }
    click(app,id);
    if(WaitForSingleObject(thread,7000)==WAIT_OBJECT_0) GetExitCodeThread(thread,&result);
    CHECK("owned confirmation answered / defaults No",result==0 && answer.default_no);
    CloseHandle(thread); return answer;
}
static void credentials(GuiApp *app,int admin)
{
    click(app,admin ? GUI_ADMIN : GUI_STUDENT);
    SetWindowTextW(app->id,admin ? L"A1" : L"U1"); SetWindowTextW(app->password,password);
}
static void register_user(GuiApp *app,int admin)
{
    click(app,admin ? GUI_ADMIN : GUI_STUDENT); click(app,GUI_SWITCH);
    SetWindowTextW(app->id,admin ? L"A1" : L"U1");
    SetWindowTextW(app->name,admin ? L"演示管理员" : L"演示学生甲 Mixed");
    SetWindowTextW(app->password,password); SetWindowTextW(app->confirmation,password);
    CHECK("password edit masks input",(GetWindowLongPtrW(app->password,GWL_STYLE)&ES_PASSWORD)!=0);
    click(app,GUI_SUBMIT);
    CHECK("Chinese name registration through GUI",app->page==GUI_LOGIN && !app->notice_error && !GetWindowTextLengthW(app->password));
}
static void login_user(GuiApp *app,int admin)
{
    credentials(app,admin); click(app,GUI_SUBMIT);
    CHECK("GUI login authenticates expected role",app->page==GUI_DASHBOARD && app->session.role==(admin ? ROLE_ADMIN : ROLE_STUDENT));
}
static void book(GuiApp *app,int computer,int slot)
{
    click(app,GUI_BOOKING); set_date(app,GUI_DATE,tomorrow); combo(app,GUI_SLOT,slot);
    click(app,GUI_SEAT_FIRST+computer-1); modal(app,GUI_RESERVE,IDYES);
    CHECK("future booking saved and refreshed",!app->notice_error && app->seats_valid && app->seats.symbols[computer-1]=='#');
}
static int layout_valid(GuiApp *app)
{
    RECT client,rect,previous;
    size_t i,j;
    GetClientRect(app->window,&client);
    for(i=0;i<app->child_count;++i) {
        wchar_t name[32];
        GetClassNameW(app->children[i],name,32); GetWindowRect(app->children[i],&rect); MapWindowPoints(NULL,app->window,(POINT *)&rect,2);
        if(!wcscmp(name,L"ComboBox")) rect.bottom=rect.top+gui_scale(app,38);
        if(rect.left<0 || rect.top<0 || rect.right>client.right+1 || rect.bottom>client.bottom+1 || rect.right<=rect.left || rect.bottom<=rect.top) return 0;
        for(j=0;j<i;++j) {
            RECT intersection;
            GetClassNameW(app->children[j],name,32); GetWindowRect(app->children[j],&previous); MapWindowPoints(NULL,app->window,(POINT *)&previous,2);
            if(!wcscmp(name,L"ComboBox")) previous.bottom=previous.top+gui_scale(app,38);
            if(IntersectRect(&intersection,&rect,&previous)) return 0;
        }
    }
    if(app->table) {
        HDC dc=GetDC(app->table);
        HGDIOBJ old;
        int column,sum=0;
        if(!dc) return 0;
        old=SelectObject(dc,app->body);
        for(column=0;column<Header_GetItemCount(ListView_GetHeader(app->table));++column) {
            wchar_t text[64]={0}; SIZE size={0}; LVCOLUMNW value={0};
            value.mask=LVCF_TEXT; value.pszText=text; value.cchTextMax=64;
            SendMessageW(app->table,LVM_GETCOLUMNW,(WPARAM)column,(LPARAM)&value);
            GetTextExtentPoint32W(dc,text,(int)wcslen(text),&size);
            if(ListView_GetColumnWidth(app->table,column)<size.cx+gui_scale(app,12)) { SelectObject(dc,old); ReleaseDC(app->table,dc); return 0; }
            sum+=ListView_GetColumnWidth(app->table,column);
        }
        SelectObject(dc,old); ReleaseDC(app->table,dc);
        GetClientRect(app->table,&rect);
        if(sum>rect.right) return 0;
    }
    return 1;
}
static void real_dpi_layout(GuiApp *app)
{
    const struct {GuiPage page; int admin,weekly;} pages[]={
        {GUI_LOGIN,0,0},{GUI_REGISTER,0,0},{GUI_DASHBOARD,0,0},{GUI_SEATS,0,0},{GUI_RESERVATIONS,0,0},{GUI_FAULT,0,0},{GUI_STUDENT_STATS,0,0},
        {GUI_DASHBOARD,1,0},{GUI_SEATS,1,0},{GUI_ADMIN_LAB,1,0},{GUI_ADMIN_REPORTS,1,0},{GUI_ADMIN_USAGE,1,0},{GUI_ADMIN_STUDENTS,1,0},{GUI_ADMIN_STATS,1,0},{GUI_ADMIN_STATS,1,1}};
    AuthSession student={0},admin={0};
    size_t i;
    int size;
    CHECK("layout sessions use actual account APIs",auth_login_student("U1","FinalTest9!",&student)==AUTH_OK && auth_login_admin("A1","FinalTest9!",&admin)==AUTH_OK);
    printf("ACTUAL_SYSTEM_DPI %u; no injected WM_DPICHANGED; monitors %d\n",app->dpi,GetSystemMetrics(SM_CMONITORS));
    for(size=0;size<3;++size) {
        int valid=1;
        RECT rect={0,0,gui_scale(app,size ? 1280 : 1000),gui_scale(app,size ? 800 : 650)};
        ShowWindow(app->window,SW_RESTORE); AdjustWindowRectEx(&rect,WS_OVERLAPPEDWINDOW,FALSE,0);
        SetWindowPos(app->window,NULL,50,50,rect.right-rect.left,rect.bottom-rect.top,SWP_NOZORDER);
        if(size==2) ShowWindow(app->window,SW_MAXIMIZE);
        for(i=0;i<sizeof pages/sizeof pages[0];++i) {
            app->session=pages[i].admin ? admin : student; app->role=app->session.role;
            gui_show_page(app,pages[i].page);
            if(pages[i].weekly) combo(app,GUI_ADMIN_STATS_MODE,1);
            pump(app); valid=valid && layout_valid(app);
            if(pages[i].page==GUI_ADMIN_LAB) {
                RECT seat; GetClientRect(GetDlgItem(app->window,GUI_ADMIN_SEAT_FIRST),&seat);
                valid=valid && seat.right>=gui_scale(app,38) && seat.bottom>=gui_scale(app,34);
            }
        }
        CHECK(size==0 ? "actual DPI minimum: all 15 views / headers / controls" : size==1 ? "actual DPI normal: all 15 views / headers / controls" : "actual DPI maximized: all 15 views / headers / controls",valid);
    }
    app->session=admin; gui_show_page(app,GUI_ADMIN_LAB);
    CHECK("navigation focus stays on current page",GetDlgCtrlID(GetFocus())==GUI_NAV_ADMIN_LAB);
    gui_show_page(app,GUI_ADMIN_STUDENTS);
    CHECK("student search starts in exact-ID input",GetFocus()==app->id);
}
static int viewer(GuiApp *app,int argc,char **argv)
{
    int page=argc>2 ? atoi(argv[2]) : 2, admin=argc>3 ? atoi(argv[3]) : 0;
    int width=argc>4 ? atoi(argv[4]) : 1280, height=argc>5 ? atoi(argv[5]) : 800;
    RECT rect={0,0,gui_scale(app,width),gui_scale(app,height)};
    if(page>1 && (admin ? auth_login_admin("A1","VisualTest9!",&app->session) : auth_login_student("U1","VisualTest9!",&app->session))!=AUTH_OK) return 3;
    app->role=admin ? ROLE_ADMIN : ROLE_STUDENT; gui_show_page(app,(GuiPage)page);
    AdjustWindowRectEx(&rect,WS_OVERLAPPEDWINDOW,FALSE,0); SetWindowPos(app->window,NULL,100,80,rect.right-rect.left,rect.bottom-rect.top,SWP_NOZORDER);
    if(page==GUI_ADMIN_STUDENTS) { SetWindowTextW(app->id,L"U1"); click(app,GUI_ADMIN_QUERY); }
    if(argc>6 && atoi(argv[6])) combo(app,GUI_ADMIN_STATS_MODE,1);
    ShowWindow(app->window,SW_SHOW); pump(app); pause_at="viewer"; pause_view(app,"viewer"); return 0;
}
int main(int argc,char **argv)
{
    GuiApp app={0}; LabTime now; wchar_t error[320]; int report_id;
    if(!gui_initialize_data(error,320)) return 1;
    gui_enable_dpi(); if(!gui_create(&app,GetModuleHandleW(NULL))) return 2;
    if(argc>1 && !strcmp(argv[1],"--view")) return viewer(&app,argc,argv);
    if(argc>2 && !strcmp(argv[1],"--pause")) pause_at=argv[2];
    ShowWindow(app.window,SW_SHOWNOACTIVATE); pump(&app); pause_view(&app,"login");
    CHECK("real local clock available",utils_local_time(NULL,&now)); tomorrow=utils_date_add(now.date,1);
    register_user(&app,0); login_user(&app,0);
    { wchar_t name[NAME_LEN]; CHECK("Chinese mixed name survives UTF16 / UTF8 round trip",gui_wide(app.student_stats.name,name,NAME_LEN) && !wcscmp(name,L"演示学生甲 Mixed")); }
    pause_view(&app,"student-dashboard");
    book(&app,1,0); pause_view(&app,"booking-success");
    click(&app,GUI_NAV_RESERVATIONS); CHECK("own booking visible",app.reservations_valid && app.reservations.count==1); pause_view(&app,"reservations");
    click(&app,GUI_BOOKING); set_date(&app,GUI_DATE,tomorrow); combo(&app,GUI_SLOT,0); click(&app,GUI_SEAT_FIRST+1); click(&app,GUI_RESERVE);
    CHECK("same student / slot conflict rejected",app.notice_error);
    click(&app,GUI_NAV_RESERVATIONS); ListView_SetItemState(app.table,0,LVIS_SELECTED,LVIS_SELECTED);
    modal(&app,GUI_RES_CANCEL,IDNO); CHECK("declined cancellation keeps booking",app.reservations.count==1);
    modal(&app,GUI_RES_CANCEL,IDYES); CHECK("confirmed cancellation refreshes",app.reservations_valid && !app.reservations.count && !app.notice_error);
    book(&app,2,0); book(&app,2,1);
    click(&app,GUI_NAV_FAULT); SendMessageW(app.fault_computer,CB_SETCURSEL,1,0);
    SetWindowTextW(app.fault_detail,L"显示器偶发闪烁 Display A，请检查连接线"); pause_view(&app,"fault-form");
    modal(&app,GUI_FAULT_SUBMIT,IDYES);
    CHECK("Chinese mixed report saved from current session",app.reports_valid && app.reports.count==1 && !strcmp(app.reports.items[0].student_id,"U1") && app.reports.items[0].status==REPORT_PENDING);
    report_id=app.reports.count ? app.reports.items[0].id : 0;
    click(&app,GUI_NAV_STATS); CHECK("future bookings never fake completed hours",app.stats_result==STATS_OK && app.student_stats.completed_hours==0 && app.student_stats.active_count==2 && app.student_stats.cancelled_count==1);
    click(&app,GUI_LOGOUT); CHECK("student logout clears session",!app.session.logged_in && app.page==GUI_LOGIN);
    register_user(&app,1); login_user(&app,1); pause_view(&app,"admin-dashboard");
    CHECK("admin dashboard real pending count",app.reports_valid && app.pending_reports==1 && app.normal_count==40);
    click(&app,GUI_NAV_ADMIN_USAGE); set_date(&app,GUI_ADMIN_DATE,tomorrow); combo(&app,GUI_ADMIN_SLOT,0);
    CHECK("usage returns actual reservation student",app.usage_valid && app.usage.count==1 && !strcmp(app.usage.rows[0].student_id,"U1"));
    click(&app,GUI_NAV_ADMIN_STUDENTS); SetWindowTextW(app.id,L"U1"); click(&app,GUI_ADMIN_QUERY);
    CHECK("exact student history includes cancelled booking",app.admin_student_valid && app.admin_student.active_count==2 && app.admin_student.history_count==3); pause_view(&app,"student-query");
    click(&app,GUI_NAV_ADMIN_REPORTS); ListView_SetItemState(app.table,0,LVIS_SELECTED,LVIS_SELECTED);
    if(!strcmp(pause_at,"fault-confirm")) { click(&app,GUI_ADMIN_PROCESS); pause_view(&app,"fault-confirm"); }
    { Answer a=modal(&app,GUI_ADMIN_PROCESS,IDNO); CHECK("affected future preview before confirmation",a.rows==2 && !app.notice_error); }
    modal(&app,GUI_ADMIN_PROCESS,IDYES);
    CHECK("report confirmation sets fault and closes report",!app.notice_error && app.computers[1].status==COMPUTER_FAULT);
    { FaultReport report; CHECK("business reload confirms processed report",fault_get_report(&app.session,report_id,&report)==FAULT_OK && report.status==REPORT_PROCESSED); }
    click(&app,GUI_NAV_ADMIN_STUDENTS); SetWindowTextW(app.id,L"U1"); combo(&app,GUI_ADMIN_STUDENT_MODE,1);
    CHECK("fault cancels future bookings visible in student history",app.admin_student_valid && app.admin_student.active_count==0 && app.admin_student.records.count==3);
    click(&app,GUI_NAV_ADMIN_LAB); click(&app,GUI_ADMIN_SEAT_FIRST+1); modal(&app,GUI_ADMIN_RESTORE,IDYES);
    CHECK("restoration updates device",!app.notice_error && app.computers[1].status==COMPUTER_NORMAL); pause_view(&app,"maintenance");
    click(&app,GUI_LOGOUT); login_user(&app,0); CHECK("restoration never resurrects cancellation",app.student_stats.active_count==0);
    book(&app,3,2); click(&app,GUI_LOGOUT); login_user(&app,1);
    click(&app,GUI_NAV_ADMIN_STATS); set_date(&app,GUI_ADMIN_STATS_DATE,tomorrow); click(&app,GUI_ADMIN_REFRESH);
    CHECK("daily stats follow actual GUI booking",app.admin_stats_valid && app.daily.counts.reservation_count==1 && gui_admin_chart_value(&app,2)==1); pause_view(&app,"daily");
    combo(&app,GUI_ADMIN_STATS_MODE,1); CHECK("weekly stats follow actual GUI booking",app.admin_stats_valid && app.weekly.reservation_count==1 && app.weekly.student_count==1); pause_view(&app,"weekly");
    combo(&app,GUI_ADMIN_STATS_MODE,0); modal(&app,GUI_ADMIN_EXPORT,IDNO);
    { DailyCountList counts={0}; CHECK("declined export leaves snapshot empty",storage_load_daily_counts(&counts)==STORAGE_OK && !counts.count); storage_free_daily_counts(&counts); }
    modal(&app,GUI_ADMIN_EXPORT,IDYES);
    { DailyCountList counts={0}; CHECK("daily export persisted actual snapshot",!app.notice_error && storage_load_daily_counts(&counts)==STORAGE_OK && counts.count==1 && counts.items[0].date==tomorrow && counts.items[0].reservation_count==1); storage_free_daily_counts(&counts); }
    click(&app,GUI_LOGOUT); CHECK("admin logout clears session",app.page==GUI_LOGIN && !app.session.logged_in);
    real_dpi_layout(&app);
    DestroyWindow(app.window); pump(&app);
    CHECK("new seat font and all page resources released",!app.seat_number && !app.body && !app.child_count && !app.admin_student.records.items);
    printf("%d/%d passed\n",total-failed,total); return failed ? 1 : 0;
}
