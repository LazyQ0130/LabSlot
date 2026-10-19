#include "gui_theme.h"
#include "gui_login.h"
#include "gui_dashboard.h"
#include "gui_booking.h"
#include "gui_reservations.h"
#include "gui_fault.h"
#include "gui_student_stats.h"
#include "gui_admin.h"

/* 字体每次 DPI 变化重建；绘制时选回原对象，窗口销毁时统一释放。 */
/* 功能：创建指定逻辑字号和字重的系统字体；参数应用/字号/字重；失败返回 NULL。 */
static HFONT make_font(GuiApp *app, int size, int weight)
{
    return CreateFontW(-gui_scale(app, size), 0, 0, 0, weight, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
}
/* 功能：创建窗口拥有的 DPI 字体和画刷；参数应用；全部成功返回 1。 */
int gui_theme_create(GuiApp *app)
{
    app->body = make_font(app, 14, FW_NORMAL);
    app->small = make_font(app, 12, FW_NORMAL);
    app->heading = make_font(app, 26, FW_BOLD);
    app->hero = make_font(app, 38, FW_BOLD);
    app->number = make_font(app, 30, FW_BOLD);
    app->background = CreateSolidBrush(GUI_BG);
    app->white = CreateSolidBrush(RGB(255,255,255));
    return app->body && app->small && app->heading && app->hero && app->number &&
           app->background && app->white;
}
/* 功能：逐个释放主题 GDI 对象并清空句柄；参数应用；无返回。 */
void gui_theme_destroy(GuiApp *app)
{
    DeleteObject(app->body); DeleteObject(app->small); DeleteObject(app->heading);
    DeleteObject(app->hero); DeleteObject(app->number);
    DeleteObject(app->background); DeleteObject(app->white);
    app->body = app->small = app->heading = app->hero = app->number = NULL;
    app->background = app->white = NULL;
}
/* 功能：用临时画刷填充矩形后释放；参数 DC/物理矩形/颜色；无返回。 */
void gui_fill(HDC dc, RECT rect, COLORREF color)
{
    HBRUSH brush = CreateSolidBrush(color);
    if (brush) { FillRect(dc, &rect, brush); DeleteObject(brush); }
}
/* 功能：绘制圆角卡片并释放画笔画刷；参数应用/DC/逻辑矩形/颜色；无返回。 */
void gui_card(GuiApp *app, HDC dc, int x, int y, int width, int height, COLORREF color)
{
    HBRUSH brush = CreateSolidBrush(color);
    HPEN pen = CreatePen(PS_SOLID, 1, GUI_LINE);
    HGDIOBJ old_brush = SelectObject(dc, brush), old_pen = SelectObject(dc, pen);
    RoundRect(dc, gui_scale(app,x), gui_scale(app,y), gui_scale(app,x+width),
              gui_scale(app,y+height), gui_scale(app,12), gui_scale(app,12));
    SelectObject(dc,old_brush); SelectObject(dc,old_pen);
    DeleteObject(brush); DeleteObject(pen);
}
/* 功能：使用指定字体与对齐绘制宽字符文本；参数应用/DC/文本/矩形/字体/颜色/标志；无返回。 */
void gui_text(GuiApp *app, HDC dc, const wchar_t *text, int x, int y,
              int width, int height, HFONT font, COLORREF color, UINT flags)
{
    RECT rect = {gui_scale(app,x),gui_scale(app,y),gui_scale(app,x+width),gui_scale(app,y+height)};
    HGDIOBJ old = SelectObject(dc,font);
    SetBkMode(dc,TRANSPARENT); SetTextColor(dc,color);
    DrawTextW(dc,text,-1,&rect,flags | DT_NOPREFIX);
    SelectObject(dc,old);
}
/* 双缓冲仅绘制父窗口；WS_CLIPCHILDREN 保留原生编辑控件的绘制。 */
/* 功能：双缓冲绘制当前页面；参数应用/目标 DC；无返回。 */
void gui_paint(GuiApp *app, HDC target)
{
    RECT rect;
    HDC dc = CreateCompatibleDC(target);
    HBITMAP bitmap;
    HGDIOBJ old;
    GetClientRect(app->window,&rect);
    bitmap = CreateCompatibleBitmap(target,rect.right,rect.bottom);
    if (!dc || !bitmap) {
        if (dc) { DeleteDC(dc); }
        if (bitmap) { DeleteObject(bitmap); }
        FillRect(target,&rect,app->background);
        return;
    }
    old = SelectObject(dc,bitmap);
    FillRect(dc,&rect,app->background);
    if (app->page <= GUI_REGISTER) { gui_login_paint(app,dc); }
    else {
        gui_shell_paint(app,dc);
        if (app->page == GUI_SEATS) { gui_booking_paint(app,dc); }
        else if (app->page == GUI_RESERVATIONS) { gui_reservations_paint(app,dc); }
        else if (app->page == GUI_FAULT) { gui_fault_paint(app,dc); }
        else if (app->page == GUI_STUDENT_STATS) { gui_student_stats_paint(app,dc); }
        else if (app->page == GUI_ADMIN_LAB) { gui_admin_lab_paint(app,dc); }
        else if (app->page == GUI_ADMIN_REPORTS) { gui_admin_reports_paint(app,dc); }
        else if (app->page == GUI_ADMIN_USAGE) { gui_admin_usage_paint(app,dc); }
        else if (app->page == GUI_ADMIN_STUDENTS) { gui_admin_students_paint(app,dc); }
        else if (app->page == GUI_ADMIN_STATS) { gui_admin_stats_paint(app,dc); }
        else { gui_dashboard_paint(app,dc); }
    }
    if (app->page > GUI_REGISTER && app->notice[0]) {
        gui_text(app,dc,app->notice,256,app->height-66,app->width-288,56,app->body,
                 app->notice_error ? GUI_RED : RGB(22,128,61),DT_WORDBREAK);
    }
    BitBlt(target,0,0,rect.right,rect.bottom,dc,0,0,SRCCOPY);
    SelectObject(dc,old); DeleteObject(bitmap); DeleteDC(dc);
}
