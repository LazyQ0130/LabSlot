#ifndef LABSLOT_GUI_WIDGETS_H
#define LABSLOT_GUI_WIDGETS_H
#include "gui_app.h"
HWND gui_control(GuiApp *app, const wchar_t *class_name, const wchar_t *text,
                 DWORD style, int id);
HWND gui_button(GuiApp *app, const wchar_t *text, int id);
HWND gui_edit(GuiApp *app, int id, int secret, int max_chars);
int gui_read_edit(HWND control, char *text, size_t capacity);
void gui_draw_button(GuiApp *app, const DRAWITEMSTRUCT *item);
void gui_clear_controls(GuiApp *app);
#endif
