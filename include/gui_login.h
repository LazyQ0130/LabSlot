#ifndef LABSLOT_GUI_LOGIN_H
#define LABSLOT_GUI_LOGIN_H
#include "gui_app.h"
void gui_login_create(GuiApp *app);
void gui_login_layout(GuiApp *app);
void gui_login_paint(GuiApp *app, HDC dc);
void gui_login_command(GuiApp *app, int id);
#endif
