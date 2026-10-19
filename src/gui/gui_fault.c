#include "gui_fault.h"
#include "gui_widgets.h"
#include "gui_theme.h"
#include <wchar.h>

/* 功能：取得本人表格中当前选择；参数应用；无选择返回 NULL。 */
static const FaultReport *selected_report(GuiApp *app)
{
    int row=app->table ? ListView_GetNextItem(app->table,-1,LVNI_SELECTED) : -1;
    if (!app->reports_valid || row<0 || (size_t)row>=app->reports.count) { return NULL; }
    return &app->reports.items[row];
}
/* 功能：刷新真实电脑状态和本人报告；参数应用；不改输入描述。 */
void gui_fault_refresh(GuiApp *app)
{
    Lab computers[COMPUTER_COUNT]; /* 不保存账户资料，只供电脑下拉框显示。 */
    int old=(int)SendMessageW(app->fault_computer,CB_GETCURSEL,0,0);
    int status=(int)SendMessageW(app->filter,CB_GETCURSEL,0,0)-1, i;
    wchar_t text[96];
    app->computers_result=fault_read_computers(&app->session,computers);
    SendMessageW(app->fault_computer,CB_RESETCONTENT,0,0);
    if (app->computers_result==FAULT_OK) {
        for (i=0;i<COMPUTER_COUNT;++i) {
            swprintf(text,96,L"%02d 号 · %ls",computers[i].no,computers[i].status==COMPUTER_FAULT ? L"故障" : L"正常");
            SendMessageW(app->fault_computer,CB_ADDSTRING,0,(LPARAM)text);
        }
        SendMessageW(app->fault_computer,CB_SETCURSEL,(WPARAM)(old>=0 && old<COMPUTER_COUNT ? old : 0),0);
    }
    app->reports_valid=0;
    ListView_SetItemCountEx(app->table,0,LVSICF_NOSCROLL); storage_free_reports(&app->reports);
    /* 这里必须先要求学生；底层查询也支持管理员，不能让伪造页面暴露全部报告。 */
    app->reports_result=auth_session_has_role(&app->session,ROLE_STUDENT) ?
        fault_query_reports(&app->session,0,status,&app->reports) : FAULT_UNAUTHORIZED;
    if (app->reports_result==FAULT_OK) {
        app->reports_valid=1; ListView_SetItemCountEx(app->table,(int)app->reports.count,LVSICF_NOSCROLL);
        swprintf(text,96,L"本人报告已刷新，共 %zu 条。",app->reports.count); gui_notice(app,text,0);
    } else { gui_notice_utf8(app,fault_result_message(app->reports_result),1); }
    if (app->computers_result!=FAULT_OK) { gui_notice_utf8(app,fault_result_message(app->computers_result),1); }
    ListView_SetItemState(app->table,-1,0,LVIS_SELECTED | LVIS_FOCUSED);
    EnableWindow(GetDlgItem(app->window,GUI_FAULT_SUBMIT),!app->busy && app->computers_result==FAULT_OK && app->reports_valid);
    EnableWindow(GetDlgItem(app->window,GUI_REPORT_DETAILS),FALSE);
    InvalidateRect(app->window,NULL,FALSE);
}
/* 功能：建立表单和只读本人报告表格；参数应用；无返回。 */
void gui_fault_create(GuiApp *app)
{
    const wchar_t *columns[]={L"报告编号",L"报告日期",L"电脑",L"状态",L"故障描述"};
    app->fault_computer=gui_control(app,L"COMBOBOX",L"",WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,GUI_FAULT_COMPUTER);
    app->fault_detail=gui_edit(app,GUI_FAULT_DETAIL,0,DETAIL_LEN-1);
    gui_button(app,L"确认提交报告",GUI_FAULT_SUBMIT); gui_button(app,L"刷新数据",GUI_FAULT_REFRESH);
    app->filter=gui_control(app,L"COMBOBOX",L"",WS_TABSTOP | CBS_DROPDOWNLIST,GUI_REPORT_FILTER);
    SendMessageW(app->filter,CB_ADDSTRING,0,(LPARAM)L"全部报告");
    SendMessageW(app->filter,CB_ADDSTRING,0,(LPARAM)L"待处理");
    SendMessageW(app->filter,CB_ADDSTRING,0,(LPARAM)L"已处理");
    SendMessageW(app->filter,CB_SETCURSEL,0,0);
    app->table=gui_table(app,GUI_REPORT_TABLE,columns,5);
    gui_button(app,L"查看报告详情",GUI_REPORT_DETAILS);
    gui_fault_refresh(app);
}
/* 功能：布局上下分区；参数应用；无返回。 */
void gui_fault_layout(GuiApp *app)
{
    const int weights[]={10,18,12,14,46};
    gui_move(app,GUI_FAULT_COMPUTER,280,224,160,260);
    gui_move(app,GUI_FAULT_DETAIL,460,224,app->width-516,38);
    gui_move(app,GUI_FAULT_REFRESH,280,282,130,38);
    gui_move(app,GUI_FAULT_SUBMIT,app->width-212,282,156,38);
    gui_move(app,GUI_REPORT_FILTER,app->width-248,354,192,220);
    gui_move(app,GUI_REPORT_TABLE,280,422,app->width-336,app->height-562);
    gui_move(app,GUI_REPORT_DETAILS,app->width-212,app->height-128,156,40);
    gui_table_layout(app,weights,5);
}
/* 功能：绘制表单标签、数量与空/错误状态；参数应用/DC；无返回，不读取业务。 */
void gui_fault_paint(GuiApp *app, HDC dc)
{
    wchar_t text[128];
    gui_text(app,dc,L"故障报告",256,100,app->width-288,44,app->heading,GUI_INK,0);
    gui_text(app,dc,L"报告人使用当前学生身份。提交线索后，由管理员确认处理。",256,146,app->width-288,28,app->body,GUI_MUTED,0);
    gui_card(app,dc,256,182,app->width-288,154,RGB(255,255,255));
    gui_text(app,dc,L"电脑编号",280,198,160,24,app->body,GUI_INK,0);
    gui_text(app,dc,L"故障描述（最多 255 个 UTF-8 字节）",460,198,app->width-516,24,app->body,GUI_INK,0);
    gui_text(app,dc,L"提交报告不会直接停用电脑。",430,288,app->width-664,28,app->small,GUI_MUTED,DT_WORDBREAK);
    gui_card(app,dc,256,346,app->width-288,app->height-426,RGB(255,255,255));
    gui_text(app,dc,L"我的报告",280,358,220,28,app->body,GUI_INK,0);
    if (!app->reports_valid) { wcscpy(text,L"报告读取失败，未展示旧数据；请检查后刷新。"); }
    else if (!app->reports.count) { wcscpy(text,L"这个筛选下还没有报告。发现问题时可在上方提交。"); }
    else { swprintf(text,128,L"共 %zu 条本人报告 · 双击可查看完整描述",app->reports.count); }
    gui_text(app,dc,text,280,390,app->width-336,25,app->small,app->reports_valid ? GUI_MUTED : GUI_RED,0);
}
/* 功能：确认后调用 fault_submit 并刷新本人列表；参数应用；无返回。 */
static void submit_report(GuiApp *app)
{
    char detail[DETAIL_LEN]={0};
    wchar_t wide[DETAIL_LEN], prompt[480], result_text[320], error[160];
    int no=(int)SendMessageW(app->fault_computer,CB_GETCURSEL,0,0)+1;
    FaultReport created={0};
    FaultResult result;
    if (!auth_session_has_role(&app->session,ROLE_STUDENT)) { gui_notice_utf8(app,fault_result_message(FAULT_UNAUTHORIZED),1); return; }
    if (no<1 || no>COMPUTER_COUNT || !gui_read_edit(app->fault_detail,detail,sizeof detail) ||
        !utils_valid_field(detail,sizeof detail)) {
        gui_notice(app,L"请输入合法电脑编号与故障描述：描述不能为空、超出 255 个 UTF-8 字节，或包含首尾空格、换行、控制符及 |。",1); return;
    }
    result=fault_require_role(&app->session,ROLE_STUDENT);
    if (result!=FAULT_OK) {
        gui_fault_refresh(app); gui_notice_utf8(app,fault_result_message(result),1); return;
    }
    app->busy=1; EnableWindow(GetDlgItem(app->window,GUI_FAULT_SUBMIT),FALSE);
    gui_wide(detail,wide,DETAIL_LEN);
    swprintf(prompt,480,L"电脑：%02d 号\n故障描述：%ls\n\n确认以当前学生身份提交此报告？",no,wide);
    if (MessageBoxW(app->window,prompt,L"确认故障报告",MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2)!=IDYES) {
        gui_notice(app,L"已放弃提交，描述已保留。",0); goto done;
    }
    result=fault_submit(NULL,&app->session,no,detail,&created);
    if (result==FAULT_OK) {
        SetWindowTextW(app->fault_detail,L"");
        /* 回到全部报告，避免在“已处理”筛选下把刚提交的待处理报告误认为丢失。 */
        SendMessageW(app->filter,CB_SETCURSEL,0,0); gui_fault_refresh(app);
        if (app->reports_valid) {
            swprintf(result_text,320,L"报告 %d 已提交，状态为待处理。本人报告列表已刷新。",created.id);
            gui_notice(app,result_text,0);
        } else {
            gui_wide(fault_result_message(app->reports_result),error,160);
            swprintf(result_text,320,L"报告已保存，但列表刷新失败：%ls",error); gui_notice(app,result_text,1);
        }
    } else { gui_fault_refresh(app); gui_notice_utf8(app,fault_result_message(result),1); }
done:
    app->busy=0;
    EnableWindow(GetDlgItem(app->window,GUI_FAULT_SUBMIT),app->computers_result==FAULT_OK && app->reports_valid);
}
/* 功能：处理表单、筛选和详情；参数应用/编号/通知；无返回。 */
void gui_fault_command(GuiApp *app, int id, int code)
{
    if (app->busy || app->page!=GUI_FAULT) { return; }
    if ((id==GUI_REPORT_FILTER && code==CBN_SELCHANGE) || (id==GUI_FAULT_REFRESH && code==BN_CLICKED)) { gui_fault_refresh(app); }
    else if (id==GUI_FAULT_SUBMIT && code==BN_CLICKED) { submit_report(app); }
    else if (id==GUI_REPORT_DETAILS && code==BN_CLICKED) {
        const FaultReport *report=selected_report(app);
        wchar_t description[DETAIL_LEN], text[480];
        if (!report) { return; }
        gui_wide(report->detail,description,DETAIL_LEN);
        swprintf(text,480,L"报告编号：%d\n日期：%04d-%02d-%02d\n电脑：%02d\n状态：%ls\n\n%ls",
            report->id,report->date/10000,report->date/100%100,report->date%100,report->computer_no,
            report->status==REPORT_PENDING ? L"待处理" : L"已处理",description);
        app->busy=1; MessageBoxW(app->window,text,L"故障报告详情",MB_OK | MB_ICONINFORMATION); app->busy=0;
    }
}
/* 功能：提供本人报告虚拟文本并跟踪选择；参数应用/通知；返回消息结果。 */
LRESULT gui_fault_notify(GuiApp *app, NMHDR *header)
{
    if (header->idFrom!=GUI_REPORT_TABLE) { return 0; }
    if (header->code==LVN_GETDISPINFOW) {
        NMLVDISPINFOW *info=(NMLVDISPINFOW *)header;
        wchar_t text[DETAIL_LEN]=L"";
        if (app->reports_valid && info->item.iItem>=0 && (size_t)info->item.iItem<app->reports.count) {
            const FaultReport *report=&app->reports.items[info->item.iItem];
            switch (info->item.iSubItem) {
            case 0: swprintf(text,DETAIL_LEN,L"%d",report->id); break;
            case 1: swprintf(text,DETAIL_LEN,L"%04d-%02d-%02d",report->date/10000,report->date/100%100,report->date%100); break;
            case 2: swprintf(text,DETAIL_LEN,L"%02d 号",report->computer_no); break;
            case 3: wcscpy(text,report->status==REPORT_PENDING ? L"待处理" : L"已处理"); break;
            case 4: gui_wide(report->detail,text,DETAIL_LEN); break;
            }
        }
        gui_table_text(info,text);
    } else if (!app->busy && header->code==LVN_ITEMCHANGED) {
        EnableWindow(GetDlgItem(app->window,GUI_REPORT_DETAILS),selected_report(app)!=NULL);
    } else if (!app->busy && header->code==NM_DBLCLK) { gui_fault_command(app,GUI_REPORT_DETAILS,BN_CLICKED); }
    return 0;
}
