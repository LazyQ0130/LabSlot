#include "gui_admin.h"
#include "gui_widgets.h"
#include "gui_theme.h"
#include <wchar.h>

void gui_admin_usage_refresh(GuiApp *app)
{
    app->usage_valid=0; ListView_SetItemCountEx(app->table,0,0);
    if (!gui_admin_allowed(app)) { return; }
    app->date=gui_read_date(app->date_picker);
    app->slot=(int)SendMessageW(app->slot_picker,CB_GETCURSEL,0,0);
    app->usage_result=admin_query_usage(&app->session,app->date,app->slot,&app->usage);
    app->usage_valid=app->usage_result==FAULT_OK;
    if (app->usage_valid) {
        ListView_SetItemCountEx(app->table,(int)app->usage.count,0);
        gui_notice(app,L"本页面显示有效预约信息，不代表实际签到人数。",0);
    } else { gui_admin_error(app,app->usage_result); }
    InvalidateRect(app->window,NULL,FALSE);
}
void gui_admin_usage_create(GuiApp *app)
{
    const wchar_t *columns[]={L"学号",L"姓名",L"电脑",L"预约编号"};
    app->date_picker=gui_date_control(app,GUI_ADMIN_DATE);
    app->slot_picker=gui_slot_control(app,GUI_ADMIN_SLOT);
    gui_button(app,L"刷新查询",GUI_ADMIN_REFRESH);
    app->table=gui_table(app,GUI_ADMIN_TABLE,columns,4);
    gui_admin_usage_refresh(app);
}
void gui_admin_usage_layout(GuiApp *app)
{
    int split=256+(app->width-288)*54/100;
    const int weights[]={32,28,16,24};
    gui_move(app,GUI_ADMIN_DATE,272,202,176,36);
    gui_move(app,GUI_ADMIN_SLOT,464,202,164,240);
    gui_move(app,GUI_ADMIN_REFRESH,app->width-184,106,152,40);
    gui_move(app,GUI_ADMIN_TABLE,split+16,308,app->width-split-64,app->height-408);
    gui_table_layout(app,weights,4);
}
void gui_admin_usage_paint(GuiApp *app, HDC dc)
{
    wchar_t text[160];
    int width=app->width-288, split=256+width*54/100;
    gui_admin_title(app,dc,L"实验室使用情况",L"本页面显示有效预约信息，不代表实际签到人数。只读查询，不修改学生预约。");
    gui_card(app,dc,256,190,width,app->height-270,RGB(255,255,255));
    if (app->usage_valid) {
        swprintf(text,160,L"%08d  %02d:00–%02d:00  ·  预约人数 %zu  /  空闲 %d  /  故障 %d",
            app->date,OPEN_HOUR+app->slot,OPEN_HOUR+app->slot+1,app->usage.count,app->usage.seats.free_count,app->usage.seats.fault_count);
    } else { wcscpy(text,L"数据不可用：加载失败时不保留旧预约信息。"); }
    gui_text(app,dc,text,272,250,width-32,32,app->small,app->usage_valid ? GUI_INK : GUI_RED,0);
    gui_text(app,dc,L"空闲  /  已预约  /  故障（优先显示）",272,280,split-288,24,app->small,GUI_MUTED,0);
    gui_admin_grid(app,dc,272,312,split-288,app->height-416);
    gui_text(app,dc,app->usage_valid && !app->usage.count ? L"该时段暂无有效预约" : L"预约学生明细",split+16,280,app->width-split-64,24,app->small,GUI_MUTED,0);
}
LRESULT gui_admin_usage_notify(GuiApp *app, NMHDR *header)
{
    if (header->idFrom==GUI_ADMIN_DATE && header->code==DTN_DATETIMECHANGE && !app->busy) { gui_admin_usage_refresh(app); }
    if (header->idFrom==GUI_ADMIN_TABLE && header->code==LVN_GETDISPINFOW) {
        NMLVDISPINFOW *info=(NMLVDISPINFOW *)header;
        wchar_t text[NAME_LEN]={0};
        if (app->usage_valid && info->item.iItem>=0 && (size_t)info->item.iItem<app->usage.count) {
            const AdminUsageRow *row=&app->usage.rows[info->item.iItem];
            switch (info->item.iSubItem) {
            case 0: gui_wide(row->student_id,text,NAME_LEN); break;
            case 1: gui_wide(row->name,text,NAME_LEN); break;
            case 2: swprintf(text,NAME_LEN,L"%02d",row->computer_no); break;
            case 3: swprintf(text,NAME_LEN,L"%d",row->reservation_id); break;
            }
        }
        gui_table_text(info,text);
    }
    return 0;
}
