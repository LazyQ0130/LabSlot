#ifndef LABSLOT_GUI_THEME_H
#define LABSLOT_GUI_THEME_H
#include "gui_app.h"
#define GUI_BLUE RGB(37,99,235)
#define GUI_INK RGB(15,23,42)
#define GUI_MUTED RGB(100,116,139)
#define GUI_LINE RGB(226,232,240)
#define GUI_BG RGB(248,250,252)
#define GUI_RED RGB(220,38,38)
int gui_theme_create(GuiApp *app);
void gui_theme_destroy(GuiApp *app);
void gui_fill(HDC dc, RECT rect, COLORREF color);
void gui_card(GuiApp *app, HDC dc, int x, int y, int width, int height, COLORREF color);
void gui_text(GuiApp *app, HDC dc, const wchar_t *text, int x, int y,
              int width, int height, HFONT font, COLORREF color, UINT flags);
void gui_paint(GuiApp *app, HDC target);
#endif
