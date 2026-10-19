/* 真正窗口/控件/消息测试；合成数据、可控时钟和故障注入仅在测试入口。 */
#include "gui_admin.h"
#include "gui_widgets.h"
#include "gui_theme.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

static int total, failed;
static volatile LONG clock_mode;
static volatile LONG fail_next_report_query;
FaultResult gui_real_fault_query_reports(const AuthSession *session,int no,int status,FaultReportList *out);
FaultResult fault_query_reports(const AuthSession *session,int no,int status,FaultReportList *out)
{
    /* 仅测试链接时重命名业务符号：精确模拟提交后一次刷新读取失败。 */
    if (InterlockedCompareExchange(&fail_next_report_query,0,1)==1) { return FAULT_STORAGE_ERROR; }
    return gui_real_fault_query_reports(session,no,status,out);
}
int gui_real_local_time(void *context, LabTime *now);
int utils_local_time(void *context, LabTime *now)
{
    (void)context;
    *now=(LabTime){20261019,InterlockedCompareExchange(&clock_mode,0,0)==1 ? 18 : 10,15,0};
    return 1;
}
#define CHECK(label, expr) do { ++total; if (expr) { printf("PASS %s\n",label); } else { ++failed; printf("FAIL %s line %d\n",label,__LINE__); } fflush(stdout); } while (0)
static void command(GuiApp *app,int id,int code)
{
    SendMessageW(app->window,WM_COMMAND,MAKEWPARAM(id,code),(LPARAM)GetDlgItem(app->window,id));
}
static void click(GuiApp *app,int id) { command(app,id,BN_CLICKED); }
static void pump(GuiApp *app)
{
    MSG message;
    UpdateWindow(app->window);
    while (PeekMessageW(&message,NULL,0,0,PM_REMOVE)) {
        if (message.message!=WM_QUIT && !gui_translate_message(app,&message)) { TranslateMessage(&message); DispatchMessageW(&message); }
    }
}
static void filter(GuiApp *app,int id,int index)
{
    SendMessageW(GetDlgItem(app->window,id),CB_SETCURSEL,(WPARAM)index,0);
    command(app,id,CBN_SELCHANGE); pump(app);
}
static void cell_window(HWND owner,HWND table,int row,int column,wchar_t *text,int capacity)
{
    NMLVDISPINFOW info={0};
    text[0]=L'\0';
    info.hdr.hwndFrom=table; info.hdr.idFrom=(UINT_PTR)GetDlgCtrlID(table); info.hdr.code=LVN_GETDISPINFOW;
    info.item.mask=LVIF_TEXT; info.item.iItem=row; info.item.iSubItem=column;
    info.item.pszText=text; info.item.cchTextMax=capacity;
    SendMessageW(owner,WM_NOTIFY,info.hdr.idFrom,(LPARAM)&info);
}
static void cell(GuiApp *app,int row,int column,wchar_t *text,int capacity)
{
    cell_window(app->window,app->table,row,column,text,capacity);
}
static void select_row(GuiApp *app,int row)
{
    ListView_SetItemState(app->table,row,LVIS_SELECTED | LVIS_FOCUSED,LVIS_SELECTED | LVIS_FOCUSED); pump(app);
}
static unsigned long digest(const char *path)
{
    FILE *file=fopen(path,"rb");
    unsigned long hash=2166136261u;
    int c;
    if (!file) { return 0; }
    while ((c=fgetc(file))!=EOF) { hash=(hash^(unsigned)c)*16777619u; }
    fclose(file); return hash;
}
static void corrupt(const char *path)
{
    FILE *file=fopen(path,"wb");
    CHECK("test fixture can be corrupted",file!=NULL);
    if (file) { fputs("invalid fixture\n",file); fclose(file); }
}
typedef struct {
    HWND owner, found;
    DWORD main_thread;
    int answer, count, default_no, first_id, last_id, scrolled, race, details;
    AuthSession session;
    FaultResult race_result;
} Answer;
static BOOL CALLBACK find_dialog(HWND window,LPARAM parameter)
{
    Answer *answer=(Answer *)parameter;
    wchar_t type[64];
    GetClassNameW(window,type,64);
    if (IsWindowVisible(window) && GetWindow(window,GW_OWNER)==answer->owner && wcscmp(type,answer->details ? L"#32770" : L"LabSlotAdminConfirm")==0) { answer->found=window; return FALSE; }
    return TRUE;
}
static DWORD WINAPI answer_dialog(void *parameter)
{
    Answer *answer=parameter;
    int i;
    for (i=0;i<600;++i) {
        EnumThreadWindows(answer->main_thread,find_dialog,(LPARAM)answer);
        if (answer->found) {
            /* ShowWindow 的激活消息可能尚未返回，待初始焦点设置完成再观察。 */
            Sleep(30);
            HWND table=GetDlgItem(answer->found,GUI_ADMIN_PREVIEW_TABLE);
            GUITHREADINFO info={0};
            wchar_t text[128];
            info.cbSize=sizeof info; GetGUIThreadInfo(answer->main_thread,&info);
            if (!answer->details && (!table || !IsChild(answer->found,info.hwndFocus))) { answer->found=NULL; Sleep(10); continue; }
            answer->default_no=info.hwndFocus==GetDlgItem(answer->found,GUI_ADMIN_NO);
            answer->count=table ? ListView_GetItemCount(table) : -1;
            if (table && answer->count) {
                cell_window(answer->found,table,0,0,text,128); answer->first_id=_wtoi(text);
                cell_window(answer->found,table,0,3,text,128);
                if (wcscmp(text,L"U1")!=0 && wcscmp(text,L"U2")!=0) { return 2; }
                ListView_EnsureVisible(table,answer->count-1,FALSE);
                answer->scrolled=ListView_GetTopIndex(table)>0;
                cell_window(answer->found,table,answer->count-1,0,text,128); answer->last_id=_wtoi(text);
            }
            if (answer->race==1) { answer->race_result=fault_change_computer(NULL,&answer->session,1,COMPUTER_FAULT); }
            else if (answer->race==2) { answer->race_result=fault_process(NULL,&answer->session,1,FAULT_DISMISS); }
            else if (answer->race==3) { InterlockedExchange(&fail_next_report_query,1); }
            if (answer->details) { PostMessageW(answer->found,WM_CLOSE,0,0); }
            else { PostMessageW(answer->found,WM_COMMAND,(WPARAM)answer->answer,0); }
            return 0;
        }
        Sleep(10);
    }
    return 1;
}
static Answer modal(GuiApp *app,int id,int response,int race,int details)
{
    Answer answer={0};
    DWORD status=1;
    HANDLE thread;
    answer.owner=app->window; answer.main_thread=GetCurrentThreadId(); answer.answer=response;
    answer.race=race; answer.session=app->session; answer.details=details;
    thread=CreateThread(NULL,0,answer_dialog,&answer,0,NULL);
    CHECK("modal worker created",thread!=NULL);
    if (!thread) { return answer; }
    click(app,id);
    CHECK("owned native dialog worker completed",WaitForSingleObject(thread,7000)==WAIT_OBJECT_0);
    GetExitCodeThread(thread,&status); CloseHandle(thread);
    CHECK("owned native dialog answered",status==0);
    if (!details) { CHECK("dangerous dialog defaults to No",answer.default_no); }
    return answer;
}
static int layout_valid(GuiApp *app)
{
    RECT client,rect,previous;
    size_t i,j;
    GetClientRect(app->window,&client);
    for (i=0;i<app->child_count;++i) {
        wchar_t type[32];
        GetClassNameW(app->children[i],type,32);
        GetWindowRect(app->children[i],&rect); MapWindowPoints(NULL,app->window,(POINT *)&rect,2);
        if (wcscmp(type,L"ComboBox")==0) { rect.bottom=rect.top+gui_scale(app,38); }
        if (rect.left<0 || rect.top<0 || rect.right>client.right+1 || rect.bottom>client.bottom+1 || rect.right<=rect.left || rect.bottom<=rect.top) { return 0; }
        for (j=0;j<i;++j) {
            RECT intersection;
            GetClassNameW(app->children[j],type,32);
            GetWindowRect(app->children[j],&previous); MapWindowPoints(NULL,app->window,(POINT *)&previous,2);
            if (wcscmp(type,L"ComboBox")==0) { previous.bottom=previous.top+gui_scale(app,38); }
            if (IntersectRect(&intersection,&rect,&previous)) { return 0; }
        }
    }
    if (app->table) {
        int sum=0,k,columns=Header_GetItemCount(ListView_GetHeader(app->table));
        GetClientRect(app->table,&rect);
        for (k=0;k<columns;++k) { sum+=ListView_GetColumnWidth(app->table,k); }
        if (sum>rect.right) { return 0; }
    }
    return 1;
}
static void dpi_test(GuiApp *app)
{
    const UINT dpis[]={96,120,144};
    const GuiPage pages[]={GUI_DASHBOARD,GUI_SEATS,GUI_ADMIN_LAB,GUI_ADMIN_REPORTS,GUI_ADMIN_USAGE,GUI_ADMIN_STUDENTS,GUI_ADMIN_STATS};
    int d,size,page;
    for (d=0;d<3;++d) { for (size=0;size<2;++size) {
        RECT rect={0,0,MulDiv(size ? 1280 : 1000,(int)dpis[d],96),MulDiv(size ? 800 : 650,(int)dpis[d],96)};
        AdjustWindowRectEx(&rect,WS_OVERLAPPEDWINDOW,FALSE,0);
        rect.right-=rect.left; rect.bottom-=rect.top; rect.left=rect.top=0;
        SendMessageW(app->window,WM_DPICHANGED,MAKEWPARAM(dpis[d],dpis[d]),(LPARAM)&rect);
        for (page=0;page<7;++page) {
            char label[128];
            gui_show_page(app,pages[page]); pump(app);
            snprintf(label,128,"admin DPI %u size %d page %d no overlap / table fits",dpis[d],size,(int)pages[page]);
            CHECK(label,layout_valid(app));
        }
    } }
}
static void login(GuiApp *app,int admin)
{
    if (admin) { click(app,GUI_ADMIN); }
    SetWindowTextW(app->id,admin ? L"A1" : L"U1"); SetWindowTextW(app->password,L"AdminTest9!"); click(app,GUI_SUBMIT);
    CHECK("real login enters dashboard",app->page==GUI_DASHBOARD && app->session.role==(admin ? ROLE_ADMIN : ROLE_STUDENT));
}
static void test_stats(GuiApp *app)
{
    int i;
    click(app,GUI_NAV_ADMIN_STATS);
    CHECK("daily stats from real business snapshot",app->admin_stats_valid && app->daily.counts.reservation_count==5 && app->daily.counts.student_count==2 && app->daily.counts.reserved_hours==5 && app->daily.completed_hours==1);
    CHECK("daily utilization capacity 360",app->daily.utilization>1.38 && app->daily.utilization<1.40);
    for (i=0;i<9;++i) { CHECK("daily chart reads exact slot projection",gui_admin_chart_value(app,i)==(i==0 || i==3 || i==4 ? 1 : i==2 ? 2 : 0)); }
    filter(app,GUI_ADMIN_STATS_MODE,1);
    CHECK("full Monday through Sunday range",app->admin_stats_valid && app->weekly.start_date==20261019 && app->weekly.end_date==20261025);
    CHECK("week distinct students / hours / average",app->weekly.reservation_count==8 && app->weekly.student_count==2 && app->weekly.average_daily_hours>1.14 && app->weekly.average_daily_hours<1.15);
    CHECK("week utilization capacity 2520",app->weekly.utilization>0.31 && app->weekly.utilization<0.32);
    for (i=0;i<7;++i) { CHECK("weekly chart reads exact daily projection",gui_admin_chart_value(app,i)==(i==0 ? 5 : i==1 ? 2 : i==6 ? 1 : 0)); }
    CHECK("weekly mode disables daily export",!IsWindowEnabled(GetDlgItem(app->window,GUI_ADMIN_EXPORT)));
    filter(app,GUI_ADMIN_STATS_MODE,0);
}
int main(int argc,char **argv)
{
    GuiApp app={0};
    Lab computers[COMPUTER_COUNT];
    const char *scenario=argc>1 ? argv[1] : "normal";
    wchar_t text[400];
    int i;
    DWORD gdi,user;
    if (strcmp(scenario,"recover-restart")==0) {
        CHECK("normal GUI startup replays pending maintenance",gui_initialize_data(text,400));
        CHECK("startup recovery reopens writes",storage_writes_allowed());
    }
    CHECK("isolated storage initializes",storage_initialize(computers));
    gui_enable_dpi(); CHECK("actual Win32 window created",gui_create(&app,GetModuleHandleW(NULL))!=NULL);
    if (!app.window) { return 1; }
    login(&app,strcmp(scenario,"student")!=0);
    ShowWindow(app.window,SW_SHOWNOACTIVATE); pump(&app);
    if (strcmp(scenario,"preview")==0) {
        MSG message;
        const char *page=argc>2 ? argv[2] : "dashboard";
        if (strcmp(page,"lab")==0) { click(&app,GUI_NAV_ADMIN_LAB); click(&app,GUI_ADMIN_SEAT_FIRST); }
        else if (strcmp(page,"reports")==0) { click(&app,GUI_NAV_ADMIN_REPORTS); select_row(&app,0); click(&app,GUI_ADMIN_PROCESS); }
        else if (strcmp(page,"usage")==0) { click(&app,GUI_NAV_ADMIN_USAGE); }
        else if (strcmp(page,"students")==0) { click(&app,GUI_NAV_ADMIN_STUDENTS); SetWindowTextW(app.id,L"U1"); filter(&app,GUI_ADMIN_STUDENT_MODE,1); }
        else if (strcmp(page,"daily")==0 || strcmp(page,"weekly")==0) { click(&app,GUI_NAV_ADMIN_STATS); if (strcmp(page,"weekly")==0) { filter(&app,GUI_ADMIN_STATS_MODE,1); } }
        pump(&app);
        while (GetMessageW(&message,NULL,0,0)>0) { if (!gui_translate_message(&app,&message)) { TranslateMessage(&message); DispatchMessageW(&message); } }
        return 0;
    }
    if (strcmp(scenario,"recover-restart")==0) {
        ReservationList rows={0};
        CHECK("restarted GUI shows recovered computer",app.computers_valid && app.computers[0].status==COMPUTER_FAULT);
        CHECK("recovery cancels future / preserves started",storage_load_reservations(&rows)==STORAGE_OK && rows.items[0].status==BOOKING_ACTIVE && rows.items[1].status==BOOKING_CANCELLED && rows.items[2].status==BOOKING_CANCELLED);
        storage_free_reservations(&rows);
        CHECK("replayed intent archived through storage API",GetFileAttributesW(L"data/maintenance.txt")==INVALID_FILE_ATTRIBUTES && GetFileAttributesW(L"data/maintenance.done.txt")!=INVALID_FILE_ATTRIBUTES);
        goto finish;
    }
    if (strcmp(scenario,"student")==0) {
        CHECK("student keeps six real navigation items",app.child_count==10 && !GetDlgItem(app.window,GUI_NAV_ADMIN_LAB));
        for (i=GUI_NAV_ADMIN_LAB;i<=GUI_NAV_ADMIN_STATS;++i) {
            click(&app,i); CHECK("forged admin navigation denied",app.page==GUI_DASHBOARD && app.notice_error);
        }
        CHECK("student cannot invoke admin business entry",!gui_admin_allowed(&app));
        goto finish;
    }
    CHECK("admin dashboard has eight navigation items",GetDlgItem(app.window,GUI_NAV_ADMIN_STATS) && !GetDlgItem(app.window,GUI_NAV_STATS));
    if (strcmp(scenario,"empty")==0) {
        CHECK("empty dashboard real zeros",app.reports_valid && app.pending_reports==0 && app.usage_valid && app.usage.count==0);
        click(&app,GUI_NAV_ADMIN_REPORTS); CHECK("report empty state differs from failure",app.reports_valid && !app.reports.count && !app.notice_error);
        click(&app,GUI_NAV_ADMIN_USAGE); CHECK("usage empty state",app.usage_valid && !ListView_GetItemCount(app.table));
        click(&app,GUI_NAV_ADMIN_STATS); CHECK("empty stats valid / zero bars",app.admin_stats_valid && app.daily.counts.reservation_count==0 && gui_admin_chart_value(&app,0)==0);
        goto finish;
    }
    if (strcmp(scenario,"corrupt")==0) {
        CHECK("corrupt dashboard never fake zero usage",!app.usage_valid && app.notice_error);
        click(&app,GUI_NAV_ADMIN_USAGE); CHECK("corrupt usage clears list",!app.usage_valid && !ListView_GetItemCount(app.table) && app.notice_error);
        click(&app,GUI_NAV_ADMIN_STATS); CHECK("corrupt statistics unavailable / export disabled",!app.admin_stats_valid && app.notice_error && !IsWindowEnabled(GetDlgItem(app.window,GUI_ADMIN_EXPORT)));
        click(&app,GUI_NAV_ADMIN_STUDENTS); SetWindowTextW(app.id,L"U1"); click(&app,GUI_ADMIN_QUERY);
        CHECK("corrupt student query has no stale ownership",!app.admin_student_valid && !app.admin_student.records.items && app.notice_error);
        goto finish;
    }
    if (strcmp(scenario,"corrupt-reports")==0) {
        CHECK("corrupt reports dashboard error / unavailable",!app.reports_valid && app.notice_error);
        click(&app,GUI_NAV_ADMIN_REPORTS); CHECK("corrupt reports not empty success",!app.reports_valid && !app.reports.items && app.notice_error);
        goto finish;
    }
    if (strcmp(scenario,"large")==0) {
        click(&app,GUI_NAV_ADMIN_STUDENTS); SetWindowTextW(app.id,L"U1"); filter(&app,GUI_ADMIN_STUDENT_MODE,1);
        CHECK("100000 history rows in virtual ListView",app.admin_student_valid && app.admin_student.records.count==100000 && ListView_GetItemCount(app.table)==100000 && (GetWindowLongPtrW(app.table,GWL_STYLE)&LVS_OWNERDATA));
        cell(&app,99999,0,text,400); CHECK("last history virtual row",wcscmp(text,L"100000")==0);
        click(&app,GUI_NAV_ADMIN_REPORTS); CHECK("10000 reports in virtual ListView",app.reports_valid && ListView_GetItemCount(app.table)==10000);
        cell(&app,9999,0,text,400); CHECK("last report virtual row",wcscmp(text,L"10000")==0);
        goto finish;
    }
    if (strcmp(scenario,"preview-many")==0) {
        Answer answer;
        unsigned long before=digest(RESERVATIONS_PATH);
        click(&app,GUI_NAV_ADMIN_LAB); click(&app,GUI_ADMIN_SEAT_FIRST);
        answer=modal(&app,GUI_ADMIN_MARK,GUI_ADMIN_NO,0,0);
        CHECK("long affected preview has all 41 virtual rows",answer.count==41 && answer.first_id==1 && answer.last_id==41 && answer.scrolled);
        CHECK("long preview declined without writes",before==digest(RESERVATIONS_PATH));
        goto finish;
    }
    CHECK("dashboard real computer counts",app.computers_valid && app.normal_count==39 && app.fault_count==1);
    CHECK("dashboard pending reports count",app.reports_valid && app.pending_reports==3);
    CHECK("current count includes bookings on fault computer",app.usage_valid && app.usage.count==2 && app.usage.seats.booked_count==1);
    InterlockedExchange(&clock_mode,1); click(&app,GUI_ADMIN_REFRESH);
    CHECK("outside open hours distinct from zero / error",!app.current_open && !app.usage_valid && app.usage_result==FAULT_OK);
    InterlockedExchange(&clock_mode,0); click(&app,GUI_ADMIN_REFRESH);
    if (strcmp(scenario,"account-gone")==0) {
        AccountRecords *accounts=calloc(1,sizeof *accounts);
        CHECK("test account set allocated",accounts!=NULL);
        CHECK("test removes current admin through storage API",accounts && storage_save_accounts(ROLE_ADMIN,accounts,0));
        free(accounts);
        click(&app,GUI_NAV_ADMIN_STATS);
        CHECK("expired actual admin cannot compute or export",!app.admin_stats_valid && app.notice_error && !IsWindowEnabled(GetDlgItem(app.window,GUI_ADMIN_EXPORT)));
        click(&app,GUI_NAV_ADMIN_LAB); click(&app,GUI_ADMIN_SEAT_FIRST); click(&app,GUI_ADMIN_MARK);
        CHECK("expired actual admin cannot maintain computers",!app.computers_valid && app.notice_error);
        goto finish;
    }
    if (strcmp(scenario,"save-failure")==0) {
        click(&app,GUI_NAV_ADMIN_STATS); modal(&app,GUI_ADMIN_EXPORT,GUI_ADMIN_YES,0,0);
        CHECK("export save failure never claims success",app.notice_error && app.stats_result==STATS_STORAGE_ERROR && !wcsstr(app.notice,L"已更新"));
        goto finish;
    }
    if (strcmp(scenario,"recovery")==0) {
        HANDLE lock=CreateFileW(L"data/maintenance.done.txt",GENERIC_READ | GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
        CHECK("test holds completion archive to interrupt maintenance",lock!=INVALID_HANDLE_VALUE);
        click(&app,GUI_NAV_ADMIN_LAB); click(&app,GUI_ADMIN_SEAT_FIRST);
        modal(&app,GUI_ADMIN_MARK,GUI_ADMIN_YES,0,0);
        CHECK("interrupted GUI maintenance reports recovery gate",app.notice_error && wcsstr(app.notice,L"维护未完成") && !storage_writes_allowed());
        CHECK("recovery gate disables maintenance writes",!IsWindowEnabled(GetDlgItem(app.window,GUI_ADMIN_MARK)) && !IsWindowEnabled(GetDlgItem(app.window,GUI_ADMIN_RESTORE)));
        click(&app,GUI_NAV_ADMIN_REPORTS); CHECK("recovery gate blocks reports",app.notice_error && !app.reports_valid);
        click(&app,GUI_NAV_ADMIN_STATS); CHECK("recovery gate blocks export",app.notice_error && !app.admin_stats_valid && !IsWindowEnabled(GetDlgItem(app.window,GUI_ADMIN_EXPORT)));
        if (lock!=INVALID_HANDLE_VALUE) { CloseHandle(lock); }
        CHECK("intent persists for normal restart recovery",GetFileAttributesW(L"data/maintenance.txt")!=INVALID_FILE_ATTRIBUTES);
        goto finish;
    }
    if (strcmp(scenario,"recover-restart")==0) { /* Python starts this after recovery before login. */
        goto finish;
    }
    if (strcmp(scenario,"changed")==0) {
        click(&app,GUI_NAV_ADMIN_LAB); click(&app,GUI_ADMIN_SEAT_FIRST);
        modal(&app,GUI_ADMIN_MARK,GUI_ADMIN_YES,1,0);
        CHECK("maintenance rechecks changed preview",app.notice_error && wcsstr(app.notice,L"故障"));
        click(&app,GUI_NAV_ADMIN_REPORTS); select_row(&app,0);
        modal(&app,GUI_ADMIN_PROCESS,GUI_ADMIN_YES,2,0);
        CHECK("report processing rechecks competing update",app.notice_error && wcsstr(app.notice,L"处理"));
        goto finish;
    }
    test_stats(&app);
    if (strcmp(scenario,"refresh-failure")==0) {
        click(&app,GUI_NAV_ADMIN_REPORTS); select_row(&app,0);
        modal(&app,GUI_ADMIN_PROCESS,GUI_ADMIN_YES,3,0);
        CHECK("committed operation / refresh failure distinguished",app.notice_error && wcsstr(app.notice,L"提交成功") && wcsstr(app.notice,L"刷新失败"));
        goto finish;
    }
    modal(&app,GUI_ADMIN_EXPORT,GUI_ADMIN_NO,0,0);
    CHECK("declined export leaves count empty",digest(COUNTS_PATH)==2166136261u);
    modal(&app,GUI_ADMIN_EXPORT,GUI_ADMIN_YES,0,0);
    CHECK("export success says snapshot can go stale",!app.notice_error && wcsstr(app.notice,L"快照过时"));
    { DailyCountList counts={0}; CHECK("saved daily snapshot",storage_load_daily_counts(&counts)==STORAGE_OK && counts.count==1 && counts.items[0].reservation_count==5); storage_free_daily_counts(&counts); }
    click(&app,GUI_NAV_ADMIN_USAGE);
    CHECK("usage selected current slot / real count",app.usage_valid && app.usage.count==2 && ListView_GetItemCount(app.table)==2);
    cell(&app,0,0,text,400); CHECK("usage student ID",wcscmp(text,L"U1")==0);
    cell(&app,0,1,text,400); CHECK("usage student name",wcscmp(text,L"合成学生甲")==0);
    CHECK("usage four public fields only",Header_GetItemCount(ListView_GetHeader(app.table))==4);
    filter(&app,GUI_ADMIN_SLOT,3); CHECK("usage slot selection refreshes true count",app.usage_valid && app.usage.count==1 && app.usage.rows[0].reservation_id==2);
    click(&app,GUI_NAV_ADMIN_STUDENTS); SetWindowTextW(app.id,L"U1"); click(&app,GUI_ADMIN_QUERY);
    CHECK("exact student projection active and history counts",app.admin_student_valid && strcmp(app.admin_student.student_id,"U1")==0 && app.admin_student.active_count==5 && app.admin_student.history_count==6 && app.admin_student.records.count==5);
    filter(&app,GUI_ADMIN_STUDENT_MODE,1); CHECK("student history includes cancellation",app.admin_student.records.count==6);
    CHECK("student table public five columns",Header_GetItemCount(ListView_GetHeader(app.table))==5);
    for (i=0;i<6;++i) { int k; for (k=0;k<5;++k) { cell(&app,i,k,text,400); CHECK("public row never contains credentials or cached time",!wcsstr(text,L"AdminTest9!") && !wcsstr(text,L"999")); } }
    SetWindowTextW(app.id,L"U"); click(&app,GUI_ADMIN_QUERY);
    CHECK("nonexact query fails and releases previous student",!app.admin_student_valid && !app.admin_student.records.items && !ListView_GetItemCount(app.table) && app.notice_error);
    click(&app,GUI_NAV_ADMIN_LAB); click(&app,GUI_ADMIN_SEAT_FIRST);
    CHECK("selected normal computer enables mark only",app.selected==1 && IsWindowEnabled(GetDlgItem(app.window,GUI_ADMIN_MARK)) && !IsWindowEnabled(GetDlgItem(app.window,GUI_ADMIN_RESTORE)));
    {
        unsigned long bookings=digest(RESERVATIONS_PATH), devices=digest(COMPUTERS_PATH);
        Answer answer=modal(&app,GUI_ADMIN_MARK,GUI_ADMIN_NO,0,0);
        CHECK("scrollable affected preview contains exact future bookings",answer.count==2 && answer.first_id==2);
        CHECK("declined maintenance does not write",bookings==digest(RESERVATIONS_PATH) && devices==digest(COMPUTERS_PATH));
    }
    modal(&app,GUI_ADMIN_MARK,GUI_ADMIN_YES,0,0);
    CHECK("confirmed fault refreshes computer state",!app.notice_error && app.computers_valid && app.computers[0].status==COMPUTER_FAULT);
    { ReservationList rows={0}; CHECK("fault cancels future / keeps started",storage_load_reservations(&rows)==STORAGE_OK && rows.items[0].status==BOOKING_ACTIVE && rows.items[1].status==BOOKING_CANCELLED && rows.items[2].status==BOOKING_CANCELLED); storage_free_reservations(&rows); }
    click(&app,GUI_ADMIN_MARK); CHECK("repeated fault returns business error",app.notice_error);
    modal(&app,GUI_ADMIN_RESTORE,GUI_ADMIN_YES,0,0);
    CHECK("restore normal refreshes computer",!app.notice_error && app.computers[0].status==COMPUTER_NORMAL);
    { ReservationList rows={0}; CHECK("restore never resurrects cancelled bookings",storage_load_reservations(&rows)==STORAGE_OK && rows.items[1].status==BOOKING_CANCELLED && rows.items[2].status==BOOKING_CANCELLED); storage_free_reservations(&rows); }
    click(&app,GUI_NAV_ADMIN_REPORTS);
    CHECK("all reports across students virtual table",app.reports_valid && app.reports.count==4 && (GetWindowLongPtrW(app.table,GWL_STYLE)&LVS_OWNERDATA));
    filter(&app,GUI_ADMIN_REPORT_STATUS,1); CHECK("pending filter",app.reports.count==3);
    filter(&app,GUI_ADMIN_REPORT_PC,2); CHECK("specified computer filter",app.reports.count==1 && app.reports.items[0].id==2);
    filter(&app,GUI_ADMIN_REPORT_PC,0); select_row(&app,0);
    modal(&app,GUI_ADMIN_DETAILS,0,0,1);
    modal(&app,GUI_ADMIN_PROCESS,GUI_ADMIN_YES,0,0);
    CHECK("confirm fault processed / refreshed pending list",!app.notice_error && app.reports.count==2 && app.computers[2].status==COMPUTER_FAULT);
    select_row(&app,0); filter(&app,GUI_ADMIN_REPORT_ACTION,1);
    modal(&app,GUI_ADMIN_PROCESS,GUI_ADMIN_YES,0,0);
    CHECK("repair report restores computer and closes",!app.notice_error && app.reports.count==1 && app.computers[1].status==COMPUTER_NORMAL);
    select_row(&app,0); filter(&app,GUI_ADMIN_REPORT_ACTION,2);
    modal(&app,GUI_ADMIN_PROCESS,GUI_ADMIN_YES,0,0);
    CHECK("dismiss only closes / leaves normal computer",!app.notice_error && app.reports.count==0 && app.computers[3].status==COMPUTER_NORMAL);
    filter(&app,GUI_ADMIN_REPORT_STATUS,2); CHECK("processed reports correct status",app.reports.count==4);
    cell(&app,0,4,text,400); CHECK("processed status rendered",wcscmp(text,L"已处理")==0);
    select_row(&app,0); click(&app,GUI_ADMIN_PROCESS); CHECK("repeat report action denied by business",app.notice_error);
    click(&app,GUI_NAV_ADMIN_STATS); modal(&app,GUI_ADMIN_EXPORT,GUI_ADMIN_YES,0,0);
    { DailyCountList counts={0}; CHECK("same date export updates without duplicate",storage_load_daily_counts(&counts)==STORAGE_OK && counts.count==1 && counts.items[0].reservation_count==3); storage_free_daily_counts(&counts); }
    click(&app,GUI_HOME); CHECK("dashboard refresh reflects maintenance and reports",app.pending_reports==0 && app.fault_count==1);
    dpi_test(&app);
    for (i=0;i<10;++i) { gui_show_page(&app,GUI_ADMIN_STUDENTS); SetWindowTextW(app.id,L"U1"); click(&app,GUI_ADMIN_QUERY); gui_show_page(&app,GUI_ADMIN_REPORTS); gui_show_page(&app,GUI_ADMIN_STATS); pump(&app); }
    gdi=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS); user=GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS);
    printf("RESOURCE baseline GDI %lu USER %lu\n",(unsigned long)gdi,(unsigned long)user);
    for (i=0;i<80;++i) {
        gui_show_page(&app,GUI_ADMIN_STUDENTS); SetWindowTextW(app.id,L"U1"); click(&app,GUI_ADMIN_QUERY);
        gui_show_page(&app,GUI_ADMIN_REPORTS); gui_show_page(&app,GUI_ADMIN_LAB); gui_show_page(&app,GUI_ADMIN_STATS); pump(&app);
        if (i%20==19) { printf("RESOURCE cycle %d GDI %lu USER %lu\n",i+1,(unsigned long)GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS),(unsigned long)GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS)); }
    }
    CHECK("80 page cycles release owned lists",!app.admin_student.records.items && !app.reports.items && !app.reservations.items);
    CHECK("80 cycles no continuing GDI growth",GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)<=gdi+2);
    CHECK("80 cycles no continuing USER growth",GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS)<=user+2);
    corrupt(RESERVATIONS_PATH); click(&app,GUI_ADMIN_REFRESH);
    CHECK("refresh corruption clears valid stats and export",!app.admin_stats_valid && app.notice_error && !IsWindowEnabled(GetDlgItem(app.window,GUI_ADMIN_EXPORT)));
    click(&app,GUI_LOGOUT); CHECK("logout clears session / owned lists",app.page==GUI_LOGIN && !app.session.logged_in && !app.admin_student.records.items && !app.reports.items);
    gui_show_page(&app,GUI_ADMIN_LAB); CHECK("logged out cannot access admin page",app.page==GUI_LOGIN);
finish:
    DestroyWindow(app.window); pump(&app);
    CHECK("destroy releases page / theme / session",!app.body && !app.white && !app.child_count && !app.reports.items && !app.admin_student.records.items && !app.session.logged_in);
    printf("%d/%d passed\n",total-failed,total);
    return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
