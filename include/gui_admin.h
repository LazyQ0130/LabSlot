#ifndef LABSLOT_GUI_ADMIN_H
#define LABSLOT_GUI_ADMIN_H
#include "gui_app.h"

int gui_admin_allowed(GuiApp *app);
void gui_admin_error(GuiApp *app, FaultResult result);
void gui_admin_title(GuiApp *app, HDC dc, const wchar_t *title, const wchar_t *subtitle);
HWND gui_date_control(GuiApp *app, int id);
HWND gui_slot_control(GuiApp *app, int id);
int gui_read_date(HWND control);
void gui_admin_current(GuiApp *app);
void gui_admin_computers(GuiApp *app);
void gui_admin_reservation_cell(const Reservation *row, int column, wchar_t *text, size_t capacity);
int gui_admin_confirm(GuiApp *app, const wchar_t *summary, const ReservationList *affected);
void gui_admin_grid(GuiApp *app, HDC dc, int x, int y, int width, int height);

void gui_admin_dashboard_create(GuiApp *app);
void gui_admin_dashboard_layout(GuiApp *app);
void gui_admin_dashboard_paint(GuiApp *app, HDC dc);
void gui_admin_dashboard_refresh(GuiApp *app);
void gui_admin_lab_create(GuiApp *app);
void gui_admin_lab_layout(GuiApp *app);
void gui_admin_lab_paint(GuiApp *app, HDC dc);
void gui_admin_lab_command(GuiApp *app, int id);
void gui_admin_reports_create(GuiApp *app);
void gui_admin_reports_layout(GuiApp *app);
void gui_admin_reports_paint(GuiApp *app, HDC dc);
void gui_admin_reports_command(GuiApp *app, int id, int code);
LRESULT gui_admin_reports_notify(GuiApp *app, NMHDR *header);
void gui_admin_usage_create(GuiApp *app);
void gui_admin_usage_layout(GuiApp *app);
void gui_admin_usage_paint(GuiApp *app, HDC dc);
void gui_admin_usage_refresh(GuiApp *app);
LRESULT gui_admin_usage_notify(GuiApp *app, NMHDR *header);
void gui_admin_students_create(GuiApp *app);
void gui_admin_students_layout(GuiApp *app);
void gui_admin_students_paint(GuiApp *app, HDC dc);
void gui_admin_students_query(GuiApp *app);
LRESULT gui_admin_students_notify(GuiApp *app, NMHDR *header);
void gui_admin_stats_create(GuiApp *app);
void gui_admin_stats_layout(GuiApp *app);
void gui_admin_stats_paint(GuiApp *app, HDC dc);
void gui_admin_stats_refresh(GuiApp *app);
void gui_admin_stats_export(GuiApp *app);
int gui_admin_chart_value(const GuiApp *app, int index);
#endif
