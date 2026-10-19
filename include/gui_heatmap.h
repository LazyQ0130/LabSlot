#ifndef LABSLOT_GUI_HEATMAP_H
#define LABSLOT_GUI_HEATMAP_H
#include "gui_app.h"
void gui_heatmap_create(GuiApp *app);
void gui_heatmap_layout(GuiApp *app);
void gui_heatmap_paint(GuiApp *app, HDC dc);
void gui_heatmap_refresh(GuiApp *app);
void gui_heatmap_command(GuiApp *app, int id, int code);
#endif
