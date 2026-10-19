#ifndef LABSLOT_GUI_FAULT_H
#define LABSLOT_GUI_FAULT_H
#include "gui_app.h"
/* 故障页只持有本人报告和电脑状态投影，不写业务文件。 */
void gui_fault_create(GuiApp *app);
void gui_fault_layout(GuiApp *app);
void gui_fault_paint(GuiApp *app, HDC dc);
void gui_fault_refresh(GuiApp *app);
void gui_fault_command(GuiApp *app, int id, int code);
LRESULT gui_fault_notify(GuiApp *app, NMHDR *header);
#endif
