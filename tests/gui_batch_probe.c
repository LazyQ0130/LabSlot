#define main p5_original_main
#include "gui_final_probe.c"
#undef main
static int fail_availability;
BookingResult __real_booking_availability(const BookingContext *,const AuthSession *,int,BookingAvailability *);
BookingResult __wrap_booking_availability(const BookingContext *context,const AuthSession *session,int date,BookingAvailability *out)
{
    if(fail_availability) { fail_availability=0;return BOOKING_STORAGE_ERROR; }
    return __real_booking_availability(context,session,date,out);
}
int main(int argc,char **argv)
{
    GuiApp app={0};LabTime now;wchar_t error[320];
    int i,d,size,date;DWORD gdi,user;
    const UINT dpis[]={96,120,144};
    CHECK("batch GUI initialize",gui_initialize_data(error,320));gui_enable_dpi();
    CHECK("batch GUI window",gui_create(&app,GetModuleHandleW(NULL))!=NULL);
    CHECK("batch GUI auth",auth_login_student("U1","FinalTest9!",&app.session)==AUTH_OK);
    CHECK("batch GUI actual clock",utils_local_time(NULL,&now));date=utils_date_add(now.date,1);
    gui_show_page(&app,GUI_SEATS);set_date(&app,GUI_DATE,date);combo(&app,GUI_SLOT,0);combo(&app,GUI_BOOK_DURATION,1);
    CHECK("continuous range free seats",app.booking_duration==2 && app.seats_valid && app.seats.free_count==40);
    click(&app,GUI_SEAT_FIRST);modal(&app,GUI_RESERVE,IDNO);
    { ReservationList list={0};CHECK("declined group saves zero",storage_load_reservations(&list)==STORAGE_OK && list.count==0);storage_free_reservations(&list); }
    modal(&app,GUI_RESERVE,IDYES);
    CHECK("two-hour GUI commit exact ids / summary",!app.notice_error && wcsstr(app.notice,L"编号 1、2") && wcsstr(app.notice,L"08:00–10:00"));
    combo(&app,GUI_SLOT,2);combo(&app,GUI_BOOK_DURATION,2);click(&app,GUI_SEAT_FIRST+1);
    if(argc>1 && !strcmp(argv[1],"--view")) { ShowWindow(app.window,SW_SHOW);pump(&app);pause_at="p6-batch";pause_view(&app,"p6-batch"); }
    fail_availability=1;modal(&app,GUI_RESERVE,IDYES);
    CHECK("commit success / refresh failure distinguished",app.notice_error && wcsstr(app.notice,L"已保存") && wcsstr(app.notice,L"刷新失败") && wcsstr(app.notice,L"编号 3、4、5"));
    click(&app,GUI_REFRESH);CHECK("refresh retry recovers full saved group",app.seats_valid && app.seats.symbols[1]=='#');
    click(&app,GUI_NAV_RESERVATIONS);CHECK("personal list receives five one-hour records",app.reservations_valid && app.reservations.count==5);
    click(&app,GUI_NAV_STATS);CHECK("stats receives five future hours",app.stats_result==STATS_OK && app.student_stats.active_count==5 && app.student_stats.completed_hours==0);
    click(&app,GUI_BOOKING);set_date(&app,GUI_DATE,date);combo(&app,GUI_SLOT,8);combo(&app,GUI_BOOK_DURATION,2);
    CHECK("continuous out of bounds disables seats",!app.seats_valid && app.notice_error && !IsWindowEnabled(GetDlgItem(app.window,GUI_RESERVE)));
    click(&app,GUI_SMART_OPEN);set_date(&app,GUI_SMART_DATE,date);combo(&app,GUI_SMART_SLOT,5);combo(&app,GUI_SMART_DURATION,2);click(&app,GUI_SMART_FIRST);
    CHECK("recommendation carries complete duration",app.page==GUI_SEATS && app.booking_duration==3 && app.slot==5 && app.selected>0);
    for(d=0;d<3;++d) {gui_update_dpi(&app,dpis[d]);for(size=0;size<2;++size) {
        RECT rect={0,0,gui_scale(&app,size ? 1280 : 1000),gui_scale(&app,size ? 800 : 650)};
        AdjustWindowRectEx(&rect,WS_OVERLAPPEDWINDOW,FALSE,0);SetWindowPos(app.window,NULL,0,0,rect.right-rect.left,rect.bottom-rect.top,SWP_NOZORDER);
        gui_show_page(&app,GUI_SEATS);combo(&app,GUI_BOOK_DURATION,2);pump(&app);CHECK("batch DPI no overlap / all controls fit",layout_valid(&app));
    }}
    for(i=0;i<5;++i) {gui_show_page(&app,GUI_SEATS);pump(&app);gui_show_page(&app,GUI_RECOMMEND);pump(&app);}
    gdi=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);user=GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS);
    for(i=0;i<30;++i) {gui_show_page(&app,GUI_SEATS);combo(&app,GUI_BOOK_DURATION,2);pump(&app);gui_show_page(&app,GUI_RECOMMEND);pump(&app);}
    CHECK("batch GDI stable",GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)<=gdi+2);CHECK("batch USER stable",GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS)<=user+2);
    DestroyWindow(app.window);pump(&app);CHECK("batch all GUI resources released",!app.body && !app.child_count);
    printf("%d/%d passed\n",total-failed,total);return failed ? 1 : 0;
}
