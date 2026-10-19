#define main p5_original_main
#include "gui_final_probe.c"
#undef main
#include "gui_health.h"
static void cell(GuiApp *app,int row,int column,wchar_t *text)
{
    NMLVDISPINFOW info={0};
    info.hdr.hwndFrom=app->table;info.hdr.idFrom=GUI_HEALTH_TABLE;info.hdr.code=LVN_GETDISPINFOW;
    info.item.mask=LVIF_TEXT;info.item.iItem=row;info.item.iSubItem=column;info.item.pszText=text;info.item.cchTextMax=80;
    SendMessageW(app->window,WM_NOTIFY,GUI_HEALTH_TABLE,(LPARAM)&info);
}
int main(int argc,char **argv)
{
    GuiApp app={0};wchar_t error[320],text[80];
    int i,d,size,corrupt=argc>1 && !strcmp(argv[1],"--corrupt");
    DWORD gdi,user;
    const UINT dpis[]={96,120,144};
    CHECK("health GUI initialize",gui_initialize_data(error,320));gui_enable_dpi();
    CHECK("health GUI window",gui_create(&app,GetModuleHandleW(NULL))!=NULL);
    CHECK("health actual student auth",auth_login_student("U1","FinalTest9!",&app.session)==AUTH_OK);
    gui_show_page(&app,GUI_DASHBOARD);click(&app,GUI_HEALTH_OPEN);
    CHECK("forged student health entry denied",app.page==GUI_DASHBOARD && app.notice_error && !app.health_valid);
    CHECK("health actual admin auth",auth_login_admin("A1","FinalTest9!",&app.session)==AUTH_OK);
    gui_show_page(&app,GUI_DASHBOARD);
    CHECK("admin dashboard health entry exists",GetDlgItem(app.window,GUI_HEALTH_OPEN)!=NULL);
    click(&app,GUI_HEALTH_OPEN);CHECK("health real navigation",app.page==GUI_HEALTH);
    if(corrupt) {
        CHECK("corrupt reports no untrusted scores",!app.health_valid && app.notice_error && ListView_GetItemCount(app.table)==0 && !IsWindowEnabled(GetDlgItem(app.window,GUI_HEALTH_REPORTS)));
        goto done;
    }
    CHECK("40 devices exact sorted projection",app.health_valid && ListView_GetItemCount(app.table)==40 && app.health.devices[0].computer_no==5 && app.health.devices[0].score==0 && app.health.devices[1].computer_no==2 && app.health.devices[1].score==35);
    CHECK("grade summaries exact",app.health.level_counts[0]==36 && app.health.level_counts[1]==1 && app.health.level_counts[2]==1 && app.health.level_counts[3]==2);
    cell(&app,1,1,text);CHECK("current fault explicitly known",!wcscmp(text,L"故障（已知）"));
    cell(&app,0,2,text);CHECK("exact score through actual virtual cell",!wcscmp(text,L"0"));
    cell(&app,2,4,text);CHECK("recent 30 exact aggregate",!wcscmp(text,L"4"));
    ListView_SetItemState(app.table,2,LVIS_SELECTED | LVIS_FOCUSED,LVIS_SELECTED | LVIS_FOCUSED);
    CHECK("selected detail enables report navigation",IsWindowEnabled(GetDlgItem(app.window,GUI_HEALTH_REPORTS)));
    if(argc>1 && !strcmp(argv[1],"--view")) {
        if(argc>3) {
            RECT rect={0,0,gui_scale(&app,atoi(argv[2])),gui_scale(&app,atoi(argv[3]))};
            AdjustWindowRectEx(&rect,WS_OVERLAPPEDWINDOW,FALSE,0);SetWindowPos(app.window,NULL,0,0,rect.right-rect.left,rect.bottom-rect.top,SWP_NOZORDER);
        }
        ShowWindow(app.window,SW_SHOW);pump(&app);pause_at="p6-health";pause_view(&app,"p6-health");
    }
    click(&app,GUI_HEALTH_REPORTS);
    CHECK("related reports filters actual computer",app.page==GUI_ADMIN_REPORTS && app.reports_valid && app.reports.count==6 && SendMessageW(app.fault_computer,CB_GETCURSEL,0,0)==1);
    for(i=0;i<(int)app.reports.count;++i) { CHECK("related report belongs to chosen device",app.reports.items[i].computer_no==1); }
    for(d=0;d<3;++d) {gui_update_dpi(&app,dpis[d]);for(size=0;size<2;++size) {
        RECT rect={0,0,gui_scale(&app,size ? 1280 : 1000),gui_scale(&app,size ? 800 : 650)};
        AdjustWindowRectEx(&rect,WS_OVERLAPPEDWINDOW,FALSE,0);SetWindowPos(app.window,NULL,0,0,rect.right-rect.left,rect.bottom-rect.top,SWP_NOZORDER);
        gui_show_page(&app,GUI_HEALTH);pump(&app);CHECK("health DPI bounds / table headers fit",layout_valid(&app));
    }}
    for(i=0;i<5;++i) {gui_show_page(&app,GUI_DASHBOARD);pump(&app);gui_show_page(&app,GUI_HEALTH);pump(&app);}
    gdi=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);user=GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS);
    for(i=0;i<30;++i) {gui_show_page(&app,GUI_DASHBOARD);pump(&app);gui_show_page(&app,GUI_HEALTH);pump(&app);}
    CHECK("health GDI stable",GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)<=gdi+2);CHECK("health USER stable",GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS)<=user+2);
done:
    DestroyWindow(app.window);pump(&app);CHECK("health resources and projection released",!app.body && !app.child_count && !app.health_valid);
    printf("%d/%d passed\n",total-failed,total);return failed ? 1 : 0;
}
