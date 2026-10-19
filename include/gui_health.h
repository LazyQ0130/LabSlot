#ifndef LABSLOT_GUI_HEALTH_H
#define LABSLOT_GUI_HEALTH_H
#include "gui_app.h"
void gui_health_create(GuiApp *app);
void gui_health_layout(GuiApp *app);
void gui_health_paint(GuiApp *app,HDC dc);
void gui_health_refresh(GuiApp *app);
void gui_health_command(GuiApp *app,int id,int code);
LRESULT gui_health_notify(GuiApp *app,NMHDR *header);
#endif
