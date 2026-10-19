/* P5.2 原生 Windows 消息集成测试；合成账户与可推进时钟仅在此驱动存在。 */
#include "gui_app.h"
#include "gui_widgets.h"
#include "gui_reservations.h"
#include "gui_fault.h"
#include "gui_student_stats.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

static int total, failed;
static volatile LONG later;
static int preview;
/* src/utils.c 单独重命名编译，生产 GUI/业务源码无测试参数或环境分支。 */
int gui_real_local_time(void *context, LabTime *now);
int utils_local_time(void *context, LabTime *now)
{
    if (preview) { return gui_real_local_time(context,now); }
    *now=InterlockedCompareExchange(&later,0,0) ? (LabTime){20261019,11,0,0} : (LabTime){20261019,10,15,0};
    return 1;
}
#define CHECK(name, expr) do { ++total; if (expr) { printf("PASS %s\n",name); } else { ++failed; printf("FAIL %s line %d\n",name,__LINE__); } fflush(stdout); } while (0)
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
static void select_row(GuiApp *app,int row)
{
    ListView_SetItemState(app->table,row,LVIS_SELECTED | LVIS_FOCUSED,LVIS_SELECTED | LVIS_FOCUSED);
    pump(app);
}
static void cell(GuiApp *app,int row,int column,wchar_t *text,int capacity)
{
    NMLVDISPINFOW info={0};
    info.hdr.hwndFrom=app->table; info.hdr.idFrom=(UINT_PTR)GetDlgCtrlID(app->table); info.hdr.code=LVN_GETDISPINFOW;
    info.item.mask=LVIF_TEXT; info.item.iItem=row; info.item.iSubItem=column;
    info.item.pszText=text; info.item.cchTextMax=capacity;
    SendMessageW(app->window,WM_NOTIFY,info.hdr.idFrom,(LPARAM)&info);
}
typedef struct {
    HWND owner;
    const wchar_t *title;
    int answer, action, id;
    AuthSession session;
    BookingResult result;
} Answer;
static DWORD WINAPI answer_dialog(void *context)
{
    Answer *answer=context;
    int i;
    for (i=0;i<600;++i) {
        HWND dialog=FindWindowW(L"#32770",answer->title);
        DWORD pid=0;
        if (dialog) { GetWindowThreadProcessId(dialog,&pid); }
        if (dialog && pid==GetCurrentProcessId() && GetWindow(dialog,GW_OWNER)==answer->owner) {
            if (answer->action==1) { InterlockedExchange(&later,1); }
            else if (answer->action==2) { answer->result=booking_cancel(NULL,&answer->session,answer->id); }
            if (answer->answer==IDOK) { PostMessageW(dialog,WM_CLOSE,0,0); }
            else { PostMessageW(dialog,WM_COMMAND,(WPARAM)answer->answer,0); }
            return 0;
        }
        Sleep(10);
    }
    return 1;
}
static void modal(GuiApp *app,int id,const wchar_t *title,int response,int action)
{
    Answer answer={app->window,title,response,action,1,app->session,BOOKING_INVALID_INPUT};
    HANDLE worker=CreateThread(NULL,0,answer_dialog,&answer,0,NULL);
    DWORD exit_code=1;
    CHECK("owned modal worker created",worker!=NULL);
    if (!worker) { return; }
    click(app,id);
    CHECK("owned modal worker completed",WaitForSingleObject(worker,7000)==WAIT_OBJECT_0);
    GetExitCodeThread(worker,&exit_code);
    CHECK("real dialog located and answered",exit_code==0);
    if (action==2) { CHECK("competing cancellation saved during confirmation",answer.result==BOOKING_OK); }
    CloseHandle(worker);
}
static int row_for(GuiApp *app,int id)
{
    size_t i;
    for (i=0;i<app->reservations.count;++i) { if (app->reservations.items[i].id==id) { return (int)i; } }
    return -1;
}
static void select_booking(GuiApp *app,int id) { select_row(app,row_for(app,id)); }
static void describe_fault(GuiApp *app,int computer,const wchar_t *description)
{
    SendMessageW(app->fault_computer,CB_SETCURSEL,(WPARAM)(computer-1),0);
    SetWindowTextW(app->fault_detail,description);
}
static void seat_date(GuiApp *app)
{
    SYSTEMTIME time={0};
    NMHDR header={app->date_picker,GUI_DATE,DTN_DATETIMECHANGE};
    time.wYear=2026; time.wMonth=10; time.wDay=20;
    SendMessageW(app->date_picker,DTM_SETSYSTEMTIME,GDT_VALID,(LPARAM)&time);
    SendMessageW(app->slot_picker,CB_SETCURSEL,0,0);
    SendMessageW(app->window,WM_NOTIFY,GUI_DATE,(LPARAM)&header);
}
/* 控件位置、文字高度、表格列宽与非重叠验证；不修改操作系统显示缩放。 */
static int layout_valid(GuiApp *app)
{
    RECT client, rect, previous;
    size_t i,j;
    GetClientRect(app->window,&client);
    for (i=0;i<app->child_count;++i) {
        HWND control=app->children[i];
        wchar_t type[32];
        GetClassNameW(control,type,32);
        GetWindowRect(control,&rect); MapWindowPoints(NULL,app->window,(POINT *)&rect,2);
        if (wcscmp(type,L"ComboBox")==0) { rect.bottom=rect.top+gui_scale(app,38); }
        if (rect.left<0 || rect.top<0 || rect.right>client.right+1 || rect.bottom>client.bottom+1 ||
            rect.right<=rect.left || rect.bottom<=rect.top) { return 0; }
        for (j=0;j<i;++j) {
            wchar_t other_type[32]; RECT intersection;
            GetClassNameW(app->children[j],other_type,32);
            GetWindowRect(app->children[j],&previous); MapWindowPoints(NULL,app->window,(POINT *)&previous,2);
            if (wcscmp(other_type,L"ComboBox")==0) { previous.bottom=previous.top+gui_scale(app,38); }
            if (IntersectRect(&intersection,&rect,&previous)) { return 0; }
        }
    }
    if (app->table) {
        int columns=Header_GetItemCount(ListView_GetHeader(app->table)), sum=0, k;
        GetClientRect(app->table,&rect);
        for (k=0;k<columns;++k) { sum+=ListView_GetColumnWidth(app->table,k); }
        if (sum>rect.right) { return 0; }
    }
    return 1;
}
static void dpi_test(GuiApp *app)
{
    const UINT dpis[]={96,120,144};
    int dpi,size,page;
    for (dpi=0;dpi<3;++dpi) {
        for (size=0;size<2;++size) {
            RECT rect={0,0,MulDiv(size ? 1280 : 1000,(int)dpis[dpi],96),MulDiv(size ? 800 : 650,(int)dpis[dpi],96)};
            AdjustWindowRectEx(&rect,WS_OVERLAPPEDWINDOW,FALSE,0);
            rect.right-=rect.left; rect.bottom-=rect.top; rect.left=rect.top=0;
            SendMessageW(app->window,WM_DPICHANGED,MAKEWPARAM(dpis[dpi],dpis[dpi]),(LPARAM)&rect);
            for (page=GUI_DASHBOARD;page<=GUI_STUDENT_STATS;++page) {
                char label[120];
                gui_show_page(app,(GuiPage)page); pump(app);
                snprintf(label,sizeof label,"student DPI %u size %d page %d no overlap / table fits",dpis[dpi],size,page);
                CHECK(label,layout_valid(app));
            }
        }
    }
}
int main(int argc,char **argv)
{
    GuiApp app={0};
    Lab computers[COMPUTER_COUNT];
    const char *scenario=argc>1 ? argv[1] : "normal";
    wchar_t text[300];
    int i;
    DWORD gdi,user;
    preview=strcmp(scenario,"preview")==0;
    CHECK("isolated data initializes",storage_initialize(computers));
    gui_enable_dpi();
    CHECK("native window created",gui_create(&app,GetModuleHandleW(NULL))!=NULL);
    if (!app.window) { return 1; }
    SetWindowTextW(app.id,L"U1"); SetWindowTextW(app.password,L"StudentTest9!"); click(&app,GUI_SUBMIT);
    CHECK("real login enters student dashboard",app.page==GUI_DASHBOARD && auth_session_has_role(&app.session,ROLE_STUDENT));
    ShowWindow(app.window,SW_SHOWNOACTIVATE); pump(&app);
    if (strcmp(scenario,"preview")==0) {
        MSG message;
        GuiPage page=argc>2 && strcmp(argv[2],"fault")==0 ? GUI_FAULT :
            argc>2 && strcmp(argv[2],"stats")==0 ? GUI_STUDENT_STATS : GUI_RESERVATIONS;
        gui_show_page(&app,page); pump(&app);
        if (page==GUI_RESERVATIONS) { select_booking(&app,1); }
        if (argc>2 && strcmp(argv[2],"cancel")==0) { click(&app,GUI_RES_CANCEL); }
        while (GetMessageW(&message,NULL,0,0)>0) {
            if (!gui_translate_message(&app,&message)) { TranslateMessage(&message); DispatchMessageW(&message); }
        }
        return 0;
    }
    click(&app,GUI_NAV_RESERVATIONS);
    CHECK("reservation navigation / native virtual table",app.page==GUI_RESERVATIONS && app.table && (GetWindowLongPtrW(app.table,GWL_STYLE)&LVS_OWNERDATA));
    CHECK("reservation table has six columns",Header_GetItemCount(ListView_GetHeader(app.table))==6);
    if (strcmp(scenario,"corrupt-reservations")==0) {
        CHECK("corrupt reservations show error / no stale rows",!app.reservations_valid && app.reservations_result==BOOKING_STORAGE_ERROR && app.notice_error && ListView_GetItemCount(app.table)==0);
        CHECK("corrupt reservations disable cancellation",!IsWindowEnabled(GetDlgItem(app.window,GUI_RES_CANCEL)));
        click(&app,GUI_NAV_STATS); CHECK("corrupt source statistics unavailable",app.stats_result==STATS_STORAGE_ERROR);
        goto finish;
    }
    if (strcmp(scenario,"empty")==0) {
        CHECK("empty active query distinct from error",app.reservations_valid && !app.reservations.count && !app.notice_error);
        filter(&app,GUI_RES_FILTER,1); CHECK("empty history",app.reservations_valid && !app.reservations.count);
        filter(&app,GUI_RES_FILTER,2); CHECK("empty cancellable filter",app.reservations_valid && !app.reservations.count);
        click(&app,GUI_NAV_FAULT); CHECK("empty personal reports distinct from error",app.reports_valid && !app.reports.count && !app.notice_error);
        goto finish;
    }
    if (strcmp(scenario,"large")==0) {
        filter(&app,GUI_RES_FILTER,1);
        CHECK("99999 own history records use one virtual table",app.reservations_valid && app.reservations.count==99999 && ListView_GetItemCount(app.table)==99999);
        cell(&app,99998,0,text,300); CHECK("last virtual row accessible",wcscmp(text,L"100000")==0);
        goto finish;
    }
    CHECK("only own active bookings",app.reservations_valid && app.reservations.count==4 && row_for(&app,4)<0 && row_for(&app,3)<0);
    for (i=0;i<(int)app.reservations.count;++i) { CHECK("row belongs to logged student",strcmp(app.reservations.items[i].student_id,"U1")==0); }
    cell(&app,row_for(&app,1),1,text,300); CHECK("date displayed by ListView notification",wcscmp(text,L"2026-10-20")==0);
    filter(&app,GUI_RES_FILTER,1); CHECK("all history includes own cancelled / excludes others",app.reservations.count==5 && row_for(&app,3)>=0 && row_for(&app,4)<0);
    cell(&app,row_for(&app,3),4,text,300); CHECK("cancelled row status",wcscmp(text,L"已取消")==0);
    filter(&app,GUI_RES_FILTER,2); CHECK("cancellable filter",app.reservations.count==2 && row_for(&app,1)>=0 && row_for(&app,5)>=0);
    if (strcmp(scenario,"boundary")==0 || strcmp(scenario,"changed")==0) {
        int boundary=strcmp(scenario,"boundary")==0;
        select_booking(&app,boundary ? 5 : 1);
        modal(&app,GUI_RES_CANCEL,L"确认取消预约",IDYES,boundary ? 1 : 2);
        CHECK("cancellation rechecked after actual confirmation",app.notice_error && wcsstr(app.notice,boundary ? L"已经开始" : L"已经取消")!=NULL);
        goto finish;
    }
    if (strcmp(scenario,"normal")==0) {
        select_booking(&app,1);
        CHECK("future selection enables dangerous button",IsWindowEnabled(GetDlgItem(app.window,GUI_RES_CANCEL)));
        modal(&app,GUI_RES_DETAILS,L"预约详情",IDOK,0);
        modal(&app,GUI_RES_CANCEL,L"确认取消预约",IDNO,0);
        CHECK("declined cancellation retains active rows",app.reservations.count==2 && row_for(&app,1)>=0 && !app.notice_error);
        modal(&app,GUI_RES_CANCEL,L"确认取消预约",IDYES,0);
        CHECK("successful cancel refreshes current filter",app.reservations_valid && app.reservations.count==1 && row_for(&app,1)<0 && !app.notice_error);
        CHECK("successful cancel refreshes dashboard metrics",app.stats_result==STATS_OK && app.student_stats.active_count==3 && app.student_stats.cancelled_count==2);
        click(&app,GUI_BOOKING); seat_date(&app); CHECK("cancelled seat visible free on next seat query",app.seats_valid && app.seats.symbols[0]=='*');
        click(&app,GUI_NAV_RESERVATIONS); filter(&app,GUI_RES_FILTER,1);
        select_booking(&app,2); CHECK("already started selection disables cancel",!IsWindowEnabled(GetDlgItem(app.window,GUI_RES_CANCEL)));
        click(&app,GUI_RES_CANCEL); CHECK("forged started cancel command rejected",app.notice_error && wcsstr(app.notice,L"已经开始"));
        /* 只篡改测试展示缓存，确认 GUI 即使收到伪造记录也由业务拒绝越权。 */
        app.reservations.items[0].id=4; select_row(&app,0); click(&app,GUI_RES_CANCEL);
        CHECK("forged other student cancellation rejected",app.notice_error && wcsstr(app.notice,L"他人"));
        click(&app,GUI_NAV_STATS);
        CHECK("personal stats ignore old Student.time cache",app.stats_result==STATS_OK && app.student_stats.completed_hours==2 && app.student_stats.completed_count==2 && app.student_stats.pending_count==1 && app.student_stats.cancelled_count==2);
        CHECK("stats correct identity",strcmp(app.student_stats.student_id,"U1")==0 && strcmp(app.student_stats.name,"合成学生")==0);
        click(&app,GUI_STATS_REFRESH); CHECK("stats refresh succeeds",!app.notice_error);
    }
    click(&app,GUI_NAV_FAULT);
    if (strcmp(scenario,"corrupt-reports")==0) {
        CHECK("corrupt report data shows error / no stale rows",!app.reports_valid && app.reports_result==FAULT_STORAGE_ERROR && app.notice_error && ListView_GetItemCount(app.table)==0);
        CHECK("corrupt reports disable submit",!IsWindowEnabled(GetDlgItem(app.window,GUI_FAULT_SUBMIT)));
        goto finish;
    }
    CHECK("own report query excludes private reports",app.reports_valid && app.reports.count==2 && ListView_GetItemCount(app.table)==2);
    CHECK("40 computers loaded using real status API",SendMessageW(app.fault_computer,CB_GETCOUNT,0,0)==40 && app.computers_result==FAULT_OK);
    for (i=0;i<(int)app.reports.count;++i) { CHECK("report belongs to current student",strcmp(app.reports.items[i].student_id,"U1")==0); }
    cell(&app,0,3,text,300); CHECK("pending status displayed",wcscmp(text,L"待处理")==0);
    cell(&app,1,3,text,300); CHECK("processed status displayed",wcscmp(text,L"已处理")==0);
    filter(&app,GUI_REPORT_FILTER,1); CHECK("pending report filter",app.reports.count==1 && app.reports.items[0].status==REPORT_PENDING);
    filter(&app,GUI_REPORT_FILTER,2); CHECK("processed report filter",app.reports.count==1 && app.reports.items[0].status==REPORT_PROCESSED);
    filter(&app,GUI_REPORT_FILTER,0);
    select_row(&app,0); modal(&app,GUI_REPORT_DETAILS,L"故障报告详情",IDOK,0);
    if (strcmp(scenario,"save-failure")==0) {
        describe_fault(&app,1,L"合成写入失败场景");
        modal(&app,GUI_FAULT_SUBMIT,L"确认故障报告",IDYES,0);
        CHECK("report save failure not reported as success",app.notice_error && app.reports.count==2 && GetWindowTextLengthW(app.fault_detail)>0 && !app.busy);
        goto finish;
    }
    if (strcmp(scenario,"account-gone")==0) {
        AccountRecords *accounts=calloc(1,sizeof *accounts);
        if (!accounts) { return 1; }
        CHECK("fixture account removal using storage API",storage_save_accounts(ROLE_STUDENT,accounts,0));
        utils_clear_secret(accounts,sizeof *accounts); free(accounts);
        describe_fault(&app,1,L"会话账户已不存在"); click(&app,GUI_FAULT_SUBMIT);
        CHECK("deleted session account rejected before submit",app.notice_error && wcsstr(app.notice,L"不存在"));
        click(&app,GUI_FAULT_REFRESH); CHECK("no stale reports after missing account",!app.reports_valid && app.reports_result==FAULT_ACCOUNT_NOT_FOUND);
        click(&app,GUI_NAV_RESERVATIONS); CHECK("missing student booking query rejected",app.reservations_result==BOOKING_STUDENT_NOT_FOUND);
        click(&app,GUI_NAV_STATS); CHECK("missing student stats rejected",app.stats_result==STATS_ACCOUNT_NOT_FOUND);
        goto finish;
    }
    describe_fault(&app,1,L""); click(&app,GUI_FAULT_SUBMIT); CHECK("empty description rejected",app.notice_error && app.reports.count==2);
    describe_fault(&app,1,L"bad|description"); click(&app,GUI_FAULT_SUBMIT); CHECK("invalid description rejected",app.notice_error);
    { wchar_t long_text[257]; for (i=0;i<256;++i) { long_text[i]=L'测'; } long_text[256]=0;
      describe_fault(&app,1,long_text); click(&app,GUI_FAULT_SUBMIT); CHECK("overlength UTF8 description rejected",app.notice_error); }
    describe_fault(&app,1,L"合法描述"); SendMessageW(app.fault_computer,CB_SETCURSEL,(WPARAM)-1,0);
    click(&app,GUI_FAULT_SUBMIT); CHECK("invalid computer selection rejected",app.notice_error);
    describe_fault(&app,3,L"重复待处理报告"); modal(&app,GUI_FAULT_SUBMIT,L"确认故障报告",IDYES,0);
    CHECK("duplicate pending report rejected",app.notice_error && app.reports.count==2 && wcsstr(app.notice,L"未处理"));
    describe_fault(&app,2,L"已经故障的电脑"); modal(&app,GUI_FAULT_SUBMIT,L"确认故障报告",IDYES,0);
    CHECK("already faulted computer rejected",app.notice_error && app.reports.count==2 && wcsstr(app.notice,L"故障"));
    describe_fault(&app,1,L"合成新报告：显示器闪烁"); modal(&app,GUI_FAULT_SUBMIT,L"确认故障报告",IDNO,0);
    CHECK("report submission declined without write",app.reports.count==2 && GetWindowTextLengthW(app.fault_detail)>0);
    modal(&app,GUI_FAULT_SUBMIT,L"确认故障报告",IDYES,0);
    CHECK("report saved / own list refreshed / input cleared",app.reports_valid && app.reports.count==3 && !app.notice_error && GetWindowTextLengthW(app.fault_detail)==0);
    CHECK("report identity from authenticated session",strcmp(app.reports.items[2].student_id,"U1")==0 && app.reports.items[2].status==REPORT_PENDING);
    click(&app,GUI_HOME); click(&app,GUI_QUICK_RESERVATIONS); CHECK("dashboard reservation shortcut works",app.page==GUI_RESERVATIONS);
    click(&app,GUI_HOME); click(&app,GUI_QUICK_FAULT); CHECK("dashboard fault shortcut works",app.page==GUI_FAULT);
    click(&app,GUI_HOME); click(&app,GUI_HOME_REFRESH); CHECK("dashboard refresh shows actual state",app.stats_result==STATS_OK && app.student_stats.active_count==3);
    dpi_test(&app);
    /* 原生控件/辅助功能可能延迟建立进程缓存；先预热，再检查 80 次完整循环。 */
    for (i=0;i<10;++i) {
        gui_show_page(&app,GUI_RESERVATIONS); pump(&app);
        gui_show_page(&app,GUI_FAULT); pump(&app);
        gui_show_page(&app,GUI_STUDENT_STATS); pump(&app);
    }
    gui_show_page(&app,GUI_STUDENT_STATS); pump(&app);
    gdi=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS); user=GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS);
    for (i=0;i<80;++i) {
        gui_show_page(&app,GUI_RESERVATIONS); pump(&app);
        gui_show_page(&app,GUI_FAULT); pump(&app);
        gui_show_page(&app,GUI_STUDENT_STATS); pump(&app);
        if (i%20==19) { printf("INFO resource cycle %d GDI %lu USER %lu\n",i+1,(unsigned long)GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS),(unsigned long)GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS)); }
    }
    printf("INFO resource baseline GDI %lu USER %lu\n",(unsigned long)gdi,(unsigned long)user);
    CHECK("80 workflow cycles release both owned lists",app.reservations.items==NULL && app.reports.items==NULL);
    CHECK("80 workflow cycles no GDI growth",GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)<=gdi+2);
    CHECK("80 workflow cycles no USER growth",GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS)<=user+2);
    click(&app,GUI_NAV_FAULT); click(&app,GUI_LOGOUT);
    CHECK("logout destroys list / form and clears session",app.page==GUI_LOGIN && !app.session.logged_in && !app.reports.items && !app.reservations.items && !app.table);
    click(&app,GUI_NAV_RESERVATIONS); gui_show_page(&app,GUI_RESERVATIONS); CHECK("logged-out booking navigation denied",app.page==GUI_LOGIN);
    gui_show_page(&app,GUI_FAULT); CHECK("logged-out report navigation denied",app.page==GUI_LOGIN);
    gui_show_page(&app,GUI_STUDENT_STATS); CHECK("logged-out statistics navigation denied",app.page==GUI_LOGIN);
    click(&app,GUI_ADMIN); SetWindowTextW(app.id,L"A1"); SetWindowTextW(app.password,L"StudentTest9!"); click(&app,GUI_SUBMIT);
    CHECK("admin navigation unchanged",app.page==GUI_DASHBOARD && GetDlgItem(app.window,GUI_NAV_RESERVATIONS)==NULL);
    click(&app,GUI_NAV_FAULT); CHECK("forged admin student-page command rejected",app.page==GUI_DASHBOARD && app.notice_error && !app.reports.items);
finish:
    DestroyWindow(app.window); pump(&app);
    CHECK("destroy releases all page resources",!app.body && !app.white && !app.reservations.items && !app.reports.items && !app.session.logged_in);
    printf("%d/%d passed\n",total-failed,total);
    return failed ? 1 : 0;
}
