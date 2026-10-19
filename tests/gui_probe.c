/* 真正创建 Win32 控件并通过窗口消息执行交互；只编入测试，不进入正式 GUI。 */
#include "gui_app.h"
#include "gui_booking.h"
#include "gui_widgets.h"
#include "gui_theme.h"
#include "storage.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

static int total, failed;
typedef struct {
    HWND owner;
    int answer, date, slot, no, compete;
    AuthSession session;
    BookingResult result;
} DialogAnswer;
#define CHECK(label, condition) do { ++total; if (!(condition)) { ++failed; printf("FAIL %s (line %d)\n",label,__LINE__); } else { printf("PASS %s\n",label); } } while (0)
static const wchar_t *test_password=L"LocalTest9!"; /* 合成凭据，不输出。 */
static void click(GuiApp *app,int id) { SendMessageW(app->window,WM_COMMAND,MAKEWPARAM(id,BN_CLICKED),(LPARAM)GetDlgItem(app->window,id)); }
static void credentials(GuiApp *app,const wchar_t *id,const wchar_t *password)
{
    SetWindowTextW(app->id,id); SetWindowTextW(app->password,password);
}
static void pump(void)
{
    MSG message;
    while (PeekMessageW(&message,NULL,0,0,PM_REMOVE)) {
        if (message.message!=WM_QUIT) { TranslateMessage(&message); DispatchMessageW(&message); }
    }
}
static void date_slot(GuiApp *app,int date,int slot)
{
    SYSTEMTIME time={0};
    NMHDR notification={app->date_picker,GUI_DATE,DTN_DATETIMECHANGE};
    time.wYear=(WORD)(date/10000); time.wMonth=(WORD)(date/100%100); time.wDay=(WORD)(date%100);
    SendMessageW(app->date_picker,DTM_SETSYSTEMTIME,GDT_VALID,(LPARAM)&time);
    SendMessageW(app->window,WM_NOTIFY,GUI_DATE,(LPARAM)&notification);
    SendMessageW(app->slot_picker,CB_SETCURSEL,(WPARAM)slot,0);
    SendMessageW(app->window,WM_COMMAND,MAKEWPARAM(GUI_SLOT,CBN_SELCHANGE),(LPARAM)app->slot_picker);
}
/* 测试线程只回答本测试进程的确认框；生产程序没有自动确认或测试钩子。 */
static DWORD WINAPI confirm_dialog(void *parameter)
{
    DialogAnswer *answer=parameter;
    int i;
    for (i=0;i<500;++i) {
        HWND dialog=FindWindowW(L"#32770",L"确认预约");
        DWORD pid=0;
        if (dialog) { GetWindowThreadProcessId(dialog,&pid); }
        if (dialog && pid==GetCurrentProcessId() && GetWindow(dialog,GW_OWNER)==answer->owner) {
            if (answer->compete) {
                Reservation created;
                answer->result=booking_create(NULL,&answer->session,answer->date,answer->slot,answer->no,&created);
            }
            PostMessageW(dialog,WM_COMMAND,(WPARAM)answer->answer,0); return 0;
        }
        Sleep(10);
    }
    return 1;
}
static void confirm_reservation(GuiApp *app, DialogAnswer *answer)
{
    HANDLE thread;
    answer->owner=app->window;
    thread=CreateThread(NULL,0,confirm_dialog,answer,0,NULL);
    CHECK("confirmation worker created",thread!=NULL);
    if (!thread) { return; }
    click(app,GUI_RESERVE);
    CHECK("real confirmation dialog answered",WaitForSingleObject(thread,6000)==WAIT_OBJECT_0);
    { DWORD exit_code=1; GetExitCodeThread(thread,&exit_code); CHECK("confirmation located owned dialog",exit_code==0); }
    CloseHandle(thread);
}
static int layout_valid(GuiApp *app)
{
    RECT client, rect, previous={0};
    size_t i;
    GetClientRect(app->window,&client);
    for (i=0;i<app->child_count;++i) {
        HWND control=app->children[i];
        GetWindowRect(control,&rect); MapWindowPoints(NULL,app->window,(POINT *)&rect,2);
        if (GetDlgCtrlID(control)==GUI_SLOT) { rect.bottom=rect.top+gui_scale(app,38); }
        if (rect.left<0 || rect.top<0 || rect.right>client.right+1 || rect.bottom>client.bottom+1 ||
            rect.right<=rect.left || rect.bottom<=rect.top) { return 0; }
        if (GetDlgCtrlID(control)>=GUI_SEAT_FIRST && GetDlgCtrlID(control)<GUI_SEAT_FIRST+COMPUTER_COUNT) {
            int index=GetDlgCtrlID(control)-GUI_SEAT_FIRST;
            if (index%8 && rect.left<previous.right) { return 0; }
            previous=rect;
        }
    }
    return 1;
}
static void dpi_layout(GuiApp *app)
{
    const UINT dpis[]={96,120,144};
    AuthSession session=app->session;
    size_t i;
    int size;
    for (i=0;i<3;++i) {
        for (size=0;size<2;++size) {
            RECT rect={0,0,MulDiv(size ? 1280 : 1000,(int)dpis[i],96),MulDiv(size ? 800 : 650,(int)dpis[i],96)};
            char label[120];
            int page;
            AdjustWindowRectEx(&rect,WS_OVERLAPPEDWINDOW,FALSE,0);
            rect.right-=rect.left; rect.bottom-=rect.top; rect.left=0; rect.top=0;
            SendMessageW(app->window,WM_DPICHANGED,MAKEWPARAM(dpis[i],dpis[i]),(LPARAM)&rect);
            for (page=GUI_LOGIN;page<=GUI_SEATS;++page) {
                GuiPage target=(GuiPage)page;
                if (target>GUI_REGISTER) { app->session=session; }
                gui_show_page(app,target); pump();
                snprintf(label,sizeof label,"DPI %u size %d page %d controls inside client / seats aligned",dpis[i],size,page);
                CHECK(label,layout_valid(app));
            }
        }
    }
}
int main(int argc,char **argv)
{
    GuiApp app={0}, restarted={0};
    Lab computers[COMPUTER_COUNT];
    LabTime now;
    ReservationList reservations={0};
    AuthSession persisted={0}, other={0};
    Student second={0};
    Reservation created={0};
    DialogAnswer answer={0};
    HWND window;
    char bytes[64]={0};
    wchar_t wide[64], invalid[]={0xd800,0};
    DWORD gdi_before, user_before;
    int tomorrow, i;
    if (argc>1 && strcmp(argv[1],"--preview")==0) {
        /* 真实窗口截图专用测试场景；不向生产入口添加演示数据或假统计。 */
        if (!storage_initialize(computers)) { return 1; }
        gui_enable_dpi(); window=gui_create(&app,GetModuleHandleW(NULL));
        if (!window) { return 1; }
        if (auth_login_student("P5Student","LocalTest9!",&app.session)!=AUTH_OK) { return 1; }
        if (argc>2 && strcmp(argv[2],"admin")==0) {
            auth_logout(&app.session); auth_login_admin("P5Admin","LocalTest9!",&app.session);
        }
        gui_show_page(&app,argc>2 && strcmp(argv[2],"dashboard")==0 ? GUI_DASHBOARD :
                      argc>2 && strcmp(argv[2],"register")==0 ? GUI_REGISTER : GUI_SEATS);
        if (app.page==GUI_SEATS) {
            utils_local_time(NULL,&now); date_slot(&app,utils_date_add(now.date,1),0);
            click(&app,GUI_SEAT_FIRST+2);
        }
        if (argc>3) {
            UINT dpi=(UINT)atoi(argv[3]);
            RECT rect={0,0,MulDiv(1000,(int)dpi,96),MulDiv(650,(int)dpi,96)};
            AdjustWindowRectEx(&rect,WS_OVERLAPPEDWINDOW,FALSE,0);
            rect.right-=rect.left; rect.bottom-=rect.top; rect.left=20; rect.top=20;
            rect.right+=20; rect.bottom+=20;
            SendMessageW(window,WM_DPICHANGED,MAKEWPARAM(dpi,dpi),(LPARAM)&rect);
        }
        ShowWindow(window,SW_SHOW); UpdateWindow(window);
        { MSG message; while (GetMessageW(&message,NULL,0,0)>0) { if (!gui_translate_message(&app,&message)) { TranslateMessage(&message); DispatchMessageW(&message); } } }
        return 0;
    }
    if (argc>1 && strcmp(argv[1],"--startup")==0) {
        wchar_t error[320];
        int ok=gui_initialize_data(error,320);
        if (argc>2 && strcmp(argv[2],"blocked")==0) {
            CHECK("startup recovery/data error blocks GUI",!ok && error[0]);
        } else { CHECK("startup uses existing initialization",ok && !error[0]); }
        printf("%d/%d passed\n",total-failed,total); return failed ? 1 : 0;
    }
    if (argc>1 && (strcmp(argv[1],"--auth-error")==0 || strcmp(argv[1],"--seats-error")==0)) {
        CHECK("error fixture initializes without reset",storage_initialize(computers));
        gui_enable_dpi(); window=gui_create(&app,GetModuleHandleW(NULL));
        if (!window) { return 1; }
        credentials(&app,L"P5Student",test_password); click(&app,GUI_SUBMIT);
        if (strcmp(argv[1],"--auth-error")==0) {
            CHECK("account corruption is visible / no session",app.notice_error && app.page==GUI_LOGIN && !app.session.logged_in);
        } else {
            CHECK("dashboard reports unavailable stats",app.page==GUI_DASHBOARD && app.stats_result==STATS_STORAGE_ERROR);
            click(&app,GUI_BOOKING); click(&app,GUI_SEAT_FIRST);
            CHECK("corrupt seats never displayed as free",!app.seats_valid && app.selected==0 && app.notice_error && !IsWindowEnabled(GetDlgItem(window,GUI_SEAT_FIRST)));
            CHECK("corrupt seats cannot reserve",!IsWindowEnabled(GetDlgItem(window,GUI_RESERVE)));
        }
        DestroyWindow(window); pump();
        printf("%d/%d passed\n",total-failed,total); return failed ? 1 : 0;
    }
    gui_enable_dpi();
    CHECK("storage initializes isolated fixture",storage_initialize(computers));
    CHECK("local clock",utils_local_time(NULL,&now)); tomorrow=utils_date_add(now.date,1);
    window=gui_create(&app,GetModuleHandleW(NULL));
    CHECK("real Unicode Win32 window created",window!=NULL);
    if (!window) { return 1; }
    ShowWindow(window,SW_SHOWNOACTIVATE); UpdateWindow(window); pump();
    CHECK("login controls created",app.child_count==6 && app.id && app.password);
    CHECK("password uses ES_PASSWORD",GetWindowLongPtrW(app.password,GWL_STYLE)&ES_PASSWORD);
    CHECK("Unicode UTF8 roundtrip",gui_utf8(L"测试同学",bytes,sizeof bytes) && gui_wide(bytes,wide,64) && wcscmp(wide,L"测试同学")==0);
    CHECK("invalid surrogate rejected",!gui_utf8(invalid,bytes,sizeof bytes));
    CHECK("invalid UTF8 rejected",!gui_wide("\xff",wide,64));
    CHECK("byte overflow rejected without truncation",!gui_utf8(L"测试",bytes,6));
    CHECK("Tab advances id to password",GetNextDlgTabItem(window,app.id,FALSE)==app.password);
    click(&app,GUI_SWITCH);
    credentials(&app,L"P5Student",test_password); SetWindowTextW(app.name,L"测试同学");
    SetWindowTextW(app.confirmation,L"different"); click(&app,GUI_SUBMIT);
    CHECK("confirmation mismatch stays on registration",app.page==GUI_REGISTER && app.notice_error);
    credentials(&app,L"P5Student",test_password); SetWindowTextW(app.confirmation,test_password); click(&app,GUI_SUBMIT);
    CHECK("student registration returns to login",app.page==GUI_LOGIN && !app.session.logged_in && !app.notice_error);
    click(&app,GUI_SWITCH); credentials(&app,L"P5Student",test_password);
    SetWindowTextW(app.name,L"测试同学"); SetWindowTextW(app.confirmation,test_password); click(&app,GUI_SUBMIT);
    CHECK("duplicate student rejected",app.page==GUI_REGISTER && app.notice_error);
    click(&app,GUI_SWITCH); credentials(&app,L"P5Student",L"wrong"); click(&app,GUI_SUBMIT);
    CHECK("wrong password rejected and session empty",app.page==GUI_LOGIN && !app.session.logged_in && app.notice_error);
    CHECK("wrong password preserves id and clears password",GetWindowTextLengthW(app.id)==9 && GetWindowTextLengthW(app.password)==0);
    credentials(&app,L"P5Student",test_password);
    { MSG enter={0}; enter.hwnd=app.password; enter.message=WM_KEYDOWN; enter.wParam=VK_RETURN;
      CHECK("Enter runs primary authentication action",gui_translate_message(&app,&enter)); }
    CHECK("student login creates correct session",app.page==GUI_DASHBOARD && auth_session_has_role(&app.session,ROLE_STUDENT));
    CHECK("real dashboard statistics",app.stats_result==STATS_OK && app.student_stats.active_count==0);
    click(&app,GUI_QUICK); date_slot(&app,tomorrow,0);
    CHECK("40 real seat controls / counts",app.child_count==50 && app.seats_valid && app.seats.free_count==40);
    CHECK("date and slot selections consumed",app.date==tomorrow && app.slot==0);
    click(&app,GUI_SEAT_FIRST+2);
    CHECK("free seat selectable",app.selected==3 && IsWindowEnabled(GetDlgItem(window,GUI_RESERVE)));
    SendMessageW(GetDlgItem(window,GUI_SEAT_FIRST+2),WM_MOUSEMOVE,0,0);
    CHECK("hover feedback tracked",GetPropW(GetDlgItem(window,GUI_SEAT_FIRST+2),L"hover")!=NULL);
    SendMessageW(GetDlgItem(window,GUI_SEAT_FIRST+2),WM_MOUSELEAVE,0,0);
    date_slot(&app,tomorrow,1);
    CHECK("slot change clears old selection",app.selected==0 && app.slot==1);
    click(&app,GUI_SEAT_FIRST); date_slot(&app,utils_date_add(tomorrow,1),1);
    CHECK("date change clears old selection",app.selected==0 && app.date==utils_date_add(tomorrow,1));
    date_slot(&app,tomorrow,0); click(&app,GUI_SEAT_FIRST);
    answer.answer=IDNO; confirm_reservation(&app,&answer);
    CHECK("cancel confirmation preserves selection / no write",app.selected==1 && storage_load_reservations(&reservations)==STORAGE_OK && reservations.count==0);
    storage_free_reservations(&reservations);
    answer.answer=IDYES; confirm_reservation(&app,&answer);
    CHECK("real booking success refreshes occupied seat",app.seats_valid && app.seats.booked_count==1 && app.selected==0 && !app.notice_error);
    CHECK("booking persisted using logged student",storage_load_reservations(&reservations)==STORAGE_OK && reservations.count==1 && strcmp(reservations.items[0].student_id,"P5Student")==0);
    storage_free_reservations(&reservations);
    click(&app,GUI_SEAT_FIRST);
    CHECK("occupied seat cannot be selected",app.selected==0 && !IsWindowEnabled(GetDlgItem(window,GUI_SEAT_FIRST)));
    computers[1].status=COMPUTER_FAULT; CHECK("fixture fault saved",storage_save_computers(computers));
    click(&app,GUI_REFRESH); click(&app,GUI_SEAT_FIRST+1);
    CHECK("fault real state / cannot select",app.seats.fault_count==1 && app.seats.symbols[1]=='!' && app.selected==0 && !IsWindowEnabled(GetDlgItem(window,GUI_SEAT_FIRST+1)));
    strcpy(second.id,"P5Other"); strcpy(second.name,"Other"); strcpy(second.password,"LocalTest9!");
    CHECK("second fixture student",auth_register_student(&second)==AUTH_OK && auth_login_student(second.id,second.password,&other)==AUTH_OK);
    date_slot(&app,tomorrow,1); click(&app,GUI_SEAT_FIRST+3);
    CHECK("competing booking outside stale view",booking_create(NULL,&other,tomorrow,1,4,&created)==BOOKING_OK);
    click(&app,GUI_RESERVE);
    CHECK("stale conflict rejected / refreshed",app.notice_error && app.seats.symbols[3]=='#' && app.selected==0);
    date_slot(&app,tomorrow,2); click(&app,GUI_SEAT_FIRST+4);
    answer.compete=1; answer.session=other; answer.date=tomorrow; answer.slot=2; answer.no=5;
    confirm_reservation(&app,&answer);
    CHECK("conflict introduced during confirmation rejected",answer.result==BOOKING_OK && app.notice_error && app.seats.symbols[4]=='#' && app.selected==0);
    date_slot(&app,utils_date_add(now.date,-1),0); click(&app,GUI_SEAT_FIRST+4); click(&app,GUI_RESERVE);
    CHECK("expired slot rejected",app.notice_error);
    click(&app,GUI_LOGOUT);
    CHECK("logout clears all session fields",app.page==GUI_LOGIN && app.session.role==ROLE_NONE && !app.session.logged_in && !app.session.user_id[0]);
    click(&app,GUI_ADMIN); click(&app,GUI_SWITCH);
    credentials(&app,L"P5Admin",test_password); SetWindowTextW(app.name,L"测试管理员");
    SetWindowTextW(app.confirmation,test_password); click(&app,GUI_SUBMIT);
    CHECK("admin registration succeeds",app.page==GUI_LOGIN && !app.notice_error);
    credentials(&app,L"P5Admin",test_password); click(&app,GUI_SUBMIT);
    CHECK("admin login correct role",app.page==GUI_DASHBOARD && auth_session_has_role(&app.session,ROLE_ADMIN));
    click(&app,GUI_BOOKING);
    CHECK("role navigation names / admin has no reserve control",GetDlgItem(window,GUI_RESERVE)==NULL && app.child_count==51);
    click(&app,GUI_SEAT_FIRST+5); click(&app,GUI_RESERVE);
    CHECK("forged admin booking messages denied",app.selected==0);
    dpi_layout(&app);
    gui_show_page(&app,GUI_SEATS); pump();
    gdi_before=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
    user_before=GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS);
    for (i=0;i<80;++i) {
        gui_show_page(&app,GUI_DASHBOARD); UpdateWindow(window);
        gui_show_page(&app,GUI_SEATS); UpdateWindow(window); pump();
    }
    CHECK("80 page cycles / no GDI object growth",GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)<=gdi_before+2);
    CHECK("80 page cycles / no USER object growth",GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS)<=user_before+2);
    DestroyWindow(window); pump();
    CHECK("destroy releases resources and session",app.body==NULL && app.white==NULL && app.child_count==0 && !app.session.logged_in);
    CHECK("restart storage preserves state",storage_initialize(computers));
    CHECK("restart login restored",auth_login_student("P5Student","LocalTest9!",&persisted)==AUTH_OK);
    window=gui_create(&restarted,GetModuleHandleW(NULL)); restarted.session=persisted;
    gui_show_page(&restarted,GUI_SEATS); date_slot(&restarted,tomorrow,0);
    CHECK("restart real occupied and fault states",restarted.seats.booked_count==1 && restarted.seats.fault_count==1);
    DestroyWindow(window); pump(); auth_logout(&other); auth_logout(&persisted);
    utils_clear_secret(&second,sizeof second);
    printf("\n%d/%d passed\n",total-failed,total);
    return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
