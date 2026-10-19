#define main p5_original_main
#include "gui_final_probe.c"
#undef main
#include "gui_recommend.h"
int main(int argc, char **argv)
{
    GuiApp app={0};
    LabTime now;
    wchar_t error[320];
    int i, d, size, date;
    DWORD gdi,user;
    const UINT dpis[]={96,120,144};
    CHECK("P6 isolated initialization",gui_initialize_data(error,320));
    gui_enable_dpi(); CHECK("P6 native window",gui_create(&app,GetModuleHandleW(NULL))!=NULL);
    CHECK("P6 session via actual auth",auth_login_student("U1","FinalTest9!",&app.session)==AUTH_OK);
    CHECK("P6 local clock",utils_local_time(NULL,&now)); date=utils_date_add(now.date,1);
    gui_show_page(&app,GUI_SEATS); click(&app,GUI_SMART_OPEN);
    CHECK("recommendation entry in booking page",app.page==GUI_RECOMMEND);
    set_date(&app,GUI_SMART_DATE,date); combo(&app,GUI_SMART_SLOT,0); combo(&app,GUI_SMART_DURATION,2);
    CHECK("three real ranked results",app.recommendation_valid && app.recommendations.count==3 && app.recommendations.seats[0].computer_no==1 && app.recommendations.seats[0].score==10000);
    combo(&app,GUI_SMART_PREF,1);
    CHECK("native preference invokes business",app.recommendation_valid && app.recommendations.seats[0].position==10000);
    if(argc>1 && !strcmp(argv[1],"--view")) {
        ShowWindow(app.window,SW_SHOW); pump(&app); pause_at="p6-recommend"; pause_view(&app,"p6-recommend");
    }
    click(&app,GUI_SMART_FIRST);
    CHECK("candidate navigates and highlights actual seat",app.page==GUI_SEATS && app.date==date && app.slot==0 && app.selected==1 && app.seats_valid);
    click(&app,GUI_SMART_OPEN); set_date(&app,GUI_SMART_DATE,date); combo(&app,GUI_SMART_SLOT,8); combo(&app,GUI_SMART_DURATION,2);
    CHECK("native boundary rejects crossing 17",!app.recommendation_valid && app.notice_error && !IsWindowEnabled(GetDlgItem(app.window,GUI_SMART_FIRST)));
    for(d=0;d<3;++d) {
        gui_update_dpi(&app,dpis[d]);
        for(size=0;size<2;++size) {
            RECT rect={0,0,gui_scale(&app,size ? 1280 : 1000),gui_scale(&app,size ? 800 : 650)};
            AdjustWindowRectEx(&rect,WS_OVERLAPPEDWINDOW,FALSE,0);
            SetWindowPos(app.window,NULL,0,0,rect.right-rect.left,rect.bottom-rect.top,SWP_NOZORDER);
            gui_show_page(&app,GUI_RECOMMEND); pump(&app);
            CHECK("recommendation simulated DPI controls fit / no overlap",layout_valid(&app));
        }
    }
    gui_show_page(&app,GUI_RECOMMEND); pump(&app);
    gdi=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS); user=GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS);
    for(i=0;i<30;++i) { gui_show_page(&app,GUI_SEATS); gui_show_page(&app,GUI_RECOMMEND); pump(&app); }
    CHECK("recommendation GDI stable",GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)<=gdi+2);
    CHECK("recommendation USER stable",GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS)<=user+2);
    CHECK("admin authenticated",auth_login_admin("A1","FinalTest9!",&app.session)==AUTH_OK);
    gui_show_page(&app,GUI_DASHBOARD); click(&app,GUI_SMART_OPEN);
    CHECK("forged admin recommendation entry rejected",app.page==GUI_DASHBOARD && app.notice_error);
    DestroyWindow(app.window); pump(&app);
    CHECK("recommendation destroys resources",!app.child_count && !app.body && !app.session.logged_in);
    printf("%d/%d passed\n",total-failed,total); return failed ? 1 : 0;
}
