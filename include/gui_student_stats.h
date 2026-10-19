#ifndef LABSLOT_GUI_STUDENT_STATS_H
#define LABSLOT_GUI_STUDENT_STATS_H
#include "gui_app.h"
/* 共享刷新函数供工作台及预约成功/取消后使用，返回值保存在 stats_result。 */
void gui_student_stats_refresh(GuiApp *app);
void gui_student_stats_create(GuiApp *app);
void gui_student_stats_layout(GuiApp *app);
void gui_student_stats_paint(GuiApp *app, HDC dc);
#endif
