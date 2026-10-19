#define main p5_original_main
#include "gui_final_probe.c"
#undef main
#include "gui_heatmap.h"
static void choose(GuiApp *app,int day,int slot)
{
    RECT rect; HWND grid=GetDlgItem(app->window,GUI_HEAT_GRID);
    GetClientRect(grid,&rect);
    SendMessageW(grid,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(rect.right*(2*day+1)/14,rect.bottom*(2*slot+1)/18));
}
int main(int argc,char **argv)
{
    GuiApp app={0}; LabTime now; wchar_t error[320];
    int d,size,i,date,day;
    DWORD gdi,user;
    const UINT dpis[]={96,120,144};
    CHECK("heatmap initialization",gui_initialize_data(error,320));
    gui_enable_dpi(); CHECK("heatmap native window",gui_create(&app,GetModuleHandleW(NULL))!=NULL);
    CHECK("heatmap real student session",auth_login_student("U1","FinalTest9!",&app.session)==AUTH_OK);
    CHECK("heatmap actual clock",utils_local_time(NULL,&now));date=utils_date_add(now.date,1);
    gui_show_page(&app,GUI_SEATS); click(&app,GUI_HEAT_OPEN);
    set_date(&app,GUI_HEAT_DATE,date);click(&app,GUI_HEAT_REFRESH);
    day=utils_date_serial(date)-utils_date_serial(utils_week_start(date));
    CHECK("student booking heatmap entry / exact 40 peak",app.page==GUI_HEATMAP && app.heatmap_valid && app.heatmap.cells[day][8].count==40 && app.heatmap.cells[day][8].utilization==100.0);
    ShowWindow(app.window,SW_SHOWNOACTIVATE);pump(&app);
    choose(&app,day,8);
    CHECK("actual grid selection precise 40/40 / percent",app.heat_day==day && app.heat_slot==8 && wcsstr(app.notice,L"40/40") && wcsstr(app.notice,L"100.00%"));
    SendMessageW(GetDlgItem(app.window,GUI_HEAT_GRID),WM_KEYDOWN,VK_UP,0);
    CHECK("grid keyboard selects adjacent slot / 80 percent",app.heat_slot==7 && wcsstr(app.notice,L"32/40") && wcsstr(app.notice,L"80.00%"));
    if(argc>1 && !strcmp(argv[1],"--view")) { ShowWindow(app.window,SW_SHOW);pump(&app); pause_at="p6-heatmap";pause_view(&app,"p6-heatmap"); }
    choose(&app,day,0);click(&app,GUI_HEAT_GO);
    CHECK("future valid cell navigates existing seat query",app.page==GUI_SEATS && app.date==date && app.slot==0 && app.seats_valid);
    click(&app,GUI_HEAT_OPEN);set_date(&app,GUI_HEAT_DATE,utils_date_add(now.date,-1));click(&app,GUI_HEAT_REFRESH);
    day=utils_date_serial(utils_date_add(now.date,-1))-utils_date_serial(utils_week_start(utils_date_add(now.date,-1)));
    choose(&app,day,0);
    CHECK("historical cell cannot navigate",!IsWindowEnabled(GetDlgItem(app.window,GUI_HEAT_GO)));
    click(&app,GUI_HEAT_GO);CHECK("forged expired navigation rechecked",app.page==GUI_HEATMAP && app.notice_error);
    CHECK("heatmap real administrator session",auth_login_admin("A1","FinalTest9!",&app.session)==AUTH_OK);
    gui_show_page(&app,GUI_ADMIN_STATS);click(&app,GUI_HEAT_OPEN);
    CHECK("admin statistics heatmap entry / no student action",app.page==GUI_HEATMAP && app.heatmap_valid && !GetDlgItem(app.window,GUI_HEAT_GO));
    for(d=0;d<3;++d) { gui_update_dpi(&app,dpis[d]); for(size=0;size<2;++size) {
        RECT rect={0,0,gui_scale(&app,size ? 1280 : 1000),gui_scale(&app,size ? 800 : 650)};
        AdjustWindowRectEx(&rect,WS_OVERLAPPEDWINDOW,FALSE,0);SetWindowPos(app.window,NULL,0,0,rect.right-rect.left,rect.bottom-rect.top,SWP_NOZORDER);
        gui_show_page(&app,GUI_HEATMAP);pump(&app);CHECK("heatmap simulated DPI bounds / controls",layout_valid(&app));
    } }
    for(i=0;i<5;++i) { gui_show_page(&app,GUI_ADMIN_STATS);pump(&app);gui_show_page(&app,GUI_HEATMAP);pump(&app); }
    gdi=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);user=GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS);
    printf("RESOURCE heatmap baseline GDI %lu USER %lu\n",(unsigned long)gdi,(unsigned long)user);
    for(i=0;i<30;++i) {
        gui_show_page(&app,GUI_ADMIN_STATS);pump(&app);gui_show_page(&app,GUI_HEATMAP);pump(&app);
        if(i%10==9) { printf("RESOURCE heatmap cycle %d GDI %lu USER %lu\n",i+1,(unsigned long)GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS),(unsigned long)GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS)); }
    }
    CHECK("heatmap GDI stable",GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)<=gdi+2);
    CHECK("heatmap USER stable",GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS)<=user+2);
    DestroyWindow(app.window);pump(&app);CHECK("heatmap resources released",!app.body && !app.child_count && !app.heatmap_valid);
    printf("%d/%d passed\n",total-failed,total);return failed ? 1 : 0;
}
