#ifndef LABSLOT_GUI_RESERVATIONS_H
#define LABSLOT_GUI_RESERVATIONS_H
#include "gui_app.h"
/* 各函数参数为当前应用（绘制另带 DC）；刷新返回结果保存在应用中。 */
void gui_reservations_create(GuiApp *app);
void gui_reservations_layout(GuiApp *app);
void gui_reservations_paint(GuiApp *app, HDC dc);
void gui_reservations_refresh(GuiApp *app);
void gui_reservations_command(GuiApp *app, int id, int code);
LRESULT gui_reservations_notify(GuiApp *app, NMHDR *header);
#endif
