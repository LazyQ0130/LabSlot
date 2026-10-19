/* Real-clock full P6 workflow through production native controls and confirmations. */
#define main p5_original_main
#include "gui_final_probe.c"
#undef main
static void choose_cell(GuiApp *app,int day,int slot)
{
    HWND grid=GetDlgItem(app->window,GUI_HEAT_GRID);RECT rect;
    GetClientRect(grid,&rect);
    SendMessageW(grid,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(rect.right*(2*day+1)/14,rect.bottom*(2*slot+1)/18));
}
int main(int argc,char **argv)
{
    GuiApp app={0};LabTime now;wchar_t error[320];
    int day;
    ReservationList reservations={0};
    Answer answer;
    CHECK("P6 end-to-end isolated initialization",gui_initialize_data(error,320));
    gui_enable_dpi();CHECK("P6 end-to-end native window",gui_create(&app,GetModuleHandleW(NULL))!=NULL);
    if(argc>2 && !strcmp(argv[1],"--pause")) { pause_at=argv[2]; }
    ShowWindow(app.window,SW_SHOWNOACTIVATE);pump(&app);
    CHECK("P6 end-to-end actual clock",utils_local_time(NULL,&now));tomorrow=utils_date_add(now.date,1);
    day=utils_date_serial(tomorrow)-utils_date_serial(utils_week_start(tomorrow));
    register_user(&app,0);login_user(&app,0);
    click(&app,GUI_BOOKING);click(&app,GUI_SMART_OPEN);
    set_date(&app,GUI_SMART_DATE,tomorrow);combo(&app,GUI_SMART_SLOT,0);combo(&app,GUI_SMART_DURATION,1);
    CHECK("P6 end-to-end real recommendation",app.recommendation_valid && app.recommendations.count==3 && app.recommendations.seats[0].computer_no==1);
    pause_view(&app,"p6-recommended");click(&app,GUI_SMART_FIRST);
    CHECK("P6 recommendation carries two-hour selection",app.page==GUI_SEATS && app.booking_duration==2 && app.selected==1);
    modal(&app,GUI_RESERVE,IDYES);
    CHECK("P6 end-to-end atomic group committed",!app.notice_error && wcsstr(app.notice,L"编号 1、2") && app.student_stats.active_count==2);
    pause_view(&app,"p6-booked");
    click(&app,GUI_HEAT_OPEN);set_date(&app,GUI_HEAT_DATE,tomorrow);click(&app,GUI_HEAT_REFRESH);
    CHECK("P6 heatmap reads GUI-created group",app.heatmap_valid && app.heatmap.cells[day][0].count==1 && app.heatmap.cells[day][1].count==1 && app.heatmap.cells[day][0].utilization==2.5);
    choose_cell(&app,day,0);pause_view(&app,"p6-heatmap");click(&app,GUI_HEAT_GO);
    CHECK("P6 heatmap navigates exact existing seats",app.page==GUI_SEATS && app.date==tomorrow && app.slot==0 && app.seats.symbols[0]=='#');
    click(&app,GUI_NAV_FAULT);SendMessageW(app.fault_computer,CB_SETCURSEL,0,0);
    SetWindowTextW(app.fault_detail,L"演示线索：显示器连接线偶发松动，请检查。");modal(&app,GUI_FAULT_SUBMIT,IDYES);
    CHECK("P6 actual report submitted pending",app.reports_valid && app.reports.count==1 && app.reports.items[0].status==REPORT_PENDING);
    click(&app,GUI_LOGOUT);register_user(&app,1);login_user(&app,1);click(&app,GUI_HEALTH_OPEN);
    CHECK("P6 real report produces exact normal score 88",app.health_valid && app.health.devices[0].computer_no==1 && app.health.devices[0].status==COMPUTER_NORMAL && app.health.devices[0].score==88 && app.health.devices[0].recent_7==1 && app.health.devices[0].pending==1);
    ListView_SetItemState(app.table,0,LVIS_SELECTED | LVIS_FOCUSED,LVIS_SELECTED | LVIS_FOCUSED);
    pause_view(&app,"p6-health-report");click(&app,GUI_HEALTH_REPORTS);
    CHECK("P6 report drill-down uses actual report",app.page==GUI_ADMIN_REPORTS && app.reports_valid && app.reports.count==1 && app.reports.items[0].computer_no==1);
    ListView_SetItemState(app.table,0,LVIS_SELECTED | LVIS_FOCUSED,LVIS_SELECTED | LVIS_FOCUSED);
    combo(&app,GUI_ADMIN_REPORT_ACTION,0);answer=modal(&app,GUI_ADMIN_PROCESS,IDYES);
    CHECK("P6 known fault preview covers both group records",answer.rows==2 && !app.notice_error);
    CHECK("P6 maintenance preserves original-format cancelled history",storage_load_reservations(&reservations)==STORAGE_OK && reservations.count==2 && reservations.items[0].status==BOOKING_CANCELLED && reservations.items[1].status==BOOKING_CANCELLED);
    storage_free_reservations(&reservations);
    click(&app,GUI_HOME);click(&app,GUI_HEALTH_OPEN);
    CHECK("P6 known fault exact 27 / high risk / fact flag",app.health_valid && app.health.devices[0].computer_no==1 && app.health.devices[0].score==27 && app.health.devices[0].status==COMPUTER_FAULT && app.health.devices[0].level==HEALTH_HIGH && (app.health.devices[0].warnings&HEALTH_KNOWN_FAULT));
    ListView_SetItemState(app.table,0,LVIS_SELECTED | LVIS_FOCUSED,LVIS_SELECTED | LVIS_FOCUSED);pause_view(&app,"p6-health-known-fault");
    click(&app,GUI_NAV_ADMIN_STATS);click(&app,GUI_HEAT_OPEN);set_date(&app,GUI_HEAT_DATE,tomorrow);click(&app,GUI_HEAT_REFRESH);
    CHECK("P6 heatmap reflects real maintenance cancellation",app.heatmap_valid && app.heatmap.cells[day][0].count==0 && app.heatmap.cells[day][1].count==0);
    click(&app,GUI_NAV_ADMIN_LAB);click(&app,GUI_ADMIN_SEAT_FIRST);modal(&app,GUI_ADMIN_RESTORE,IDYES);
    CHECK("P6 actual restore succeeds",!app.notice_error && app.computers_valid && app.computers[0].status==COMPUTER_NORMAL);
    click(&app,GUI_HOME);click(&app,GUI_HEALTH_OPEN);
    CHECK("P6 restore removes known-fault penalty / report history retained",app.health_valid && app.health.devices[0].computer_no==1 && app.health.devices[0].score==92 && app.health.devices[0].pending==0 && app.health.devices[0].recent_7==1);
    CHECK("P6 restore never resurrects group",storage_load_reservations(&reservations)==STORAGE_OK && reservations.count==2 && reservations.items[0].status==BOOKING_CANCELLED && reservations.items[1].status==BOOKING_CANCELLED);
    storage_free_reservations(&reservations);click(&app,GUI_LOGOUT);
    CHECK("P6 end-to-end logout clears analysis",app.page==GUI_LOGIN && !app.session.logged_in && !app.health_valid && !app.heatmap_valid);
    DestroyWindow(app.window);pump(&app);CHECK("P6 end-to-end all resources released",!app.body && !app.child_count && !app.reports.items && !app.reservations.items);
    printf("%d/%d passed\n",total-failed,total);return failed ? 1 : 0;
}
