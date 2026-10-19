#ifndef LABSLOT_GUI_DASHBOARD_H
#define LABSLOT_GUI_DASHBOARD_H
#include "gui_app.h"
void gui_dashboard_create(GuiApp *app);
void gui_dashboard_layout(GuiApp *app);
void gui_dashboard_paint(GuiApp *app, HDC dc);
void gui_shell_create(GuiApp *app);
void gui_shell_layout(GuiApp *app);
void gui_shell_paint(GuiApp *app, HDC dc);
#endif
