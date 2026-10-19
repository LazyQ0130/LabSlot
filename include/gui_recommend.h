#ifndef LABSLOT_GUI_RECOMMEND_H
#define LABSLOT_GUI_RECOMMEND_H
#include "gui_app.h"
void gui_recommend_create(GuiApp *app);
void gui_recommend_layout(GuiApp *app);
void gui_recommend_paint(GuiApp *app, HDC dc);
void gui_recommend_refresh(GuiApp *app);
void gui_recommend_command(GuiApp *app, int id, int code);
#endif
