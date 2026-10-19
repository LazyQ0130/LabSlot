#ifndef LABSLOT_GUI_BOOKING_H
#define LABSLOT_GUI_BOOKING_H
#include "gui_app.h"
void gui_booking_create(GuiApp *app);
void gui_booking_layout(GuiApp *app);
void gui_booking_paint(GuiApp *app, HDC dc);
void gui_booking_refresh(GuiApp *app);
void gui_booking_command(GuiApp *app, int id);
#endif
