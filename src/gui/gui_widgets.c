#include "gui_widgets.h"
#include "gui_theme.h"
#include "utils.h"
#include <wchar.h>

/* 功能：为原生输入框绘制焦点边框；参数为系统子类消息；返回原生消息结果。 */
static LRESULT CALLBACK edit_proc(HWND window, UINT message, WPARAM wp, LPARAM lp,
                                   UINT_PTR subclass, DWORD_PTR data)
{
    LRESULT result;
    (void)subclass; (void)data;
    if (message==WM_NCDESTROY) { RemoveWindowSubclass(window,edit_proc,2); }
    result=DefSubclassProc(window,message,wp,lp);
    if (message==WM_SETFOCUS || message==WM_KILLFOCUS) {
        RedrawWindow(window,NULL,NULL,RDW_FRAME | RDW_INVALIDATE);
    }
    if (message==WM_NCPAINT) {
        HDC dc=GetWindowDC(window);
        RECT rect;
        HPEN pen=CreatePen(PS_SOLID,1,GetFocus()==window ? GUI_BLUE : GUI_LINE);
        HGDIOBJ old_pen, old_brush;
        GetWindowRect(window,&rect); OffsetRect(&rect,-rect.left,-rect.top);
        old_pen=SelectObject(dc,pen); old_brush=SelectObject(dc,GetStockObject(NULL_BRUSH));
        Rectangle(dc,0,0,rect.right,rect.bottom);
        SelectObject(dc,old_pen); SelectObject(dc,old_brush); DeleteObject(pen); ReleaseDC(window,dc);
    }
    return result;
}

/* 悬停/焦点只影响展示，所有选择与预约权限仍由业务接口复查。 */
/* 功能：跟踪按钮悬停及焦点；参数为系统子类消息；返回原生消息结果。 */
static LRESULT CALLBACK button_proc(HWND window, UINT message, WPARAM wp, LPARAM lp,
                                     UINT_PTR subclass, DWORD_PTR data)
{
    (void)subclass; (void)data;
    if (message == WM_MOUSEMOVE) {
        TRACKMOUSEEVENT track = {sizeof track,TME_LEAVE,window,0};
        if (!GetPropW(window,L"hover")) {
            SetPropW(window,L"hover",(HANDLE)1); TrackMouseEvent(&track);
            InvalidateRect(window,NULL,FALSE);
        }
    } else if (message == WM_MOUSELEAVE) {
        RemovePropW(window,L"hover"); InvalidateRect(window,NULL,FALSE);
    } else if (message == WM_SETFOCUS || message == WM_KILLFOCUS) {
        InvalidateRect(window,NULL,FALSE);
    } else if (message == WM_NCDESTROY) {
        RemovePropW(window,L"hover");
        RemoveWindowSubclass(window,button_proc,1);
    }
    return DefSubclassProc(window,message,wp,lp);
}
/* 功能：创建并登记窗口拥有的子控件；参数应用/类/文本/样式/编号；失败返回 NULL。 */
HWND gui_control(GuiApp *app, const wchar_t *class_name, const wchar_t *text,
                 DWORD style, int id)
{
    HWND control;
    if (app->child_count >= sizeof app->children / sizeof app->children[0]) { return NULL; }
    control = CreateWindowExW(0,class_name,text,WS_CHILD | WS_VISIBLE | style,
                 0,0,1,1,app->window,(HMENU)(INT_PTR)id,app->instance,NULL);
    if (control) {
        app->children[app->child_count++] = control;
        SendMessageW(control,WM_SETFONT,(WPARAM)app->body,TRUE);
    }
    return control;
}
/* 功能：创建可键盘访问的自绘按钮；参数应用/文本/编号；失败返回 NULL。 */
HWND gui_button(GuiApp *app, const wchar_t *text, int id)
{
    HWND control = gui_control(app,L"BUTTON",text,WS_TABSTOP | BS_OWNERDRAW,id);
    if (control) { SetWindowSubclass(control,button_proc,1,0); }
    return control;
}
/* 功能：创建有界 Unicode 输入框；参数应用/编号/密码标志/字符上限；失败返回 NULL。 */
HWND gui_edit(GuiApp *app, int id, int secret, int max_chars)
{
    HWND control = gui_control(app,L"EDIT",L"",WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL |
                                (secret ? ES_PASSWORD : 0),id);
    if (control) {
        SetWindowSubclass(control,edit_proc,2,0);
        /* 多允许一个字符：超限输入必须明确拒绝，不能截成合法短字段提交。 */
        SendMessageW(control,EM_SETLIMITTEXT,(WPARAM)(max_chars+1),0);
        SendMessageW(control,EM_SETMARGINS,EC_LEFTMARGIN | EC_RIGHTMARGIN,MAKELPARAM(10,10));
    }
    return control;
}
/* 功能：读取完整输入并安全转换，拒绝截断；参数控件/输出/字节容量；成功返回 1。 */
int gui_read_edit(HWND control, char *text, size_t capacity)
{
    wchar_t buffer[256] = {0};
    int length = GetWindowTextLengthW(control), ok = 0;
    if (length > 0 && length < (int)(sizeof buffer / sizeof buffer[0]) &&
        GetWindowTextW(control,buffer,(int)(sizeof buffer / sizeof buffer[0])) == length) {
        ok = gui_utf8(buffer,text,capacity);
    }
    utils_clear_secret(buffer,sizeof buffer);
    return ok;
}
/* 功能：先清密码再逐个销毁当前页控件；参数应用；无返回。 */
void gui_clear_controls(GuiApp *app)
{
    size_t i;
    if (app->password) { SetWindowTextW(app->password,L""); }
    if (app->confirmation) { SetWindowTextW(app->confirmation,L""); }
    for (i=0; i<app->child_count; ++i) { DestroyWindow(app->children[i]); }
    app->child_count=0;
    app->id=app->name=app->password=app->confirmation=NULL;
    app->date_picker=app->slot_picker=NULL;
}
/* 功能：按业务投影绘制按钮/座位状态；参数应用/绘制通知；无返回。 */
void gui_draw_button(GuiApp *app, const DRAWITEMSTRUCT *item)
{
    int id = (int)item->CtlID, seat = id-GUI_SEAT_FIRST;
    int enabled = !(item->itemState & ODS_DISABLED);
    int active = (id==GUI_STUDENT && app->role==ROLE_STUDENT) ||
        (id==GUI_ADMIN && app->role==ROLE_ADMIN) ||
        (id==GUI_HOME && app->page==GUI_DASHBOARD) ||
        (id==GUI_BOOKING && app->page==GUI_SEATS);
    int primary = id==GUI_SUBMIT || id==GUI_QUICK || id==GUI_RESERVE;
    int hover = GetPropW(item->hwndItem,L"hover") != NULL;
    COLORREF bg=RGB(255,255,255), fg=GUI_INK, border=GUI_LINE;
    RECT rect=item->rcItem;
    wchar_t label[128];
    HBRUSH brush;
    HPEN pen;
    HGDIOBJ old_brush, old_pen, old_font;
    if (primary) { bg=hover ? RGB(29,78,216) : GUI_BLUE; fg=RGB(255,255,255); border=bg; }
    else if (active || hover) { bg=RGB(239,246,255); fg=GUI_BLUE; border=RGB(191,219,254); }
    if (seat>=0 && seat<COMPUTER_COUNT) {
        char symbol=app->seats_valid ? app->seats.symbols[seat] : '?';
        bg=RGB(240,253,244); fg=RGB(21,128,61); border=RGB(187,247,208);
        if (symbol=='#' || symbol=='?') { bg=RGB(241,245,249); fg=GUI_MUTED; border=GUI_LINE; }
        if (symbol=='!') { bg=RGB(254,242,242); fg=GUI_RED; border=RGB(254,202,202); }
        if (enabled && hover) { border=GUI_BLUE; bg=RGB(219,234,254); }
        if (app->selected==seat+1) { bg=GUI_BLUE; fg=RGB(255,255,255); border=GUI_BLUE; }
    } else if (!enabled) { bg=RGB(241,245,249); fg=GUI_MUTED; border=GUI_LINE; }
    if (item->itemState & ODS_SELECTED) { border=GUI_INK; }
    gui_fill(item->hDC,rect,GUI_BG);
    brush=CreateSolidBrush(bg); pen=CreatePen(PS_SOLID,gui_scale(app,1),border);
    old_brush=SelectObject(item->hDC,brush); old_pen=SelectObject(item->hDC,pen);
    RoundRect(item->hDC,rect.left,rect.top,rect.right,rect.bottom,gui_scale(app,9),gui_scale(app,9));
    SelectObject(item->hDC,old_brush); SelectObject(item->hDC,old_pen);
    DeleteObject(brush); DeleteObject(pen);
    GetWindowTextW(item->hwndItem,label,128);
    old_font=SelectObject(item->hDC,app->body);
    SetBkMode(item->hDC,TRANSPARENT); SetTextColor(item->hDC,fg);
    DrawTextW(item->hDC,label,-1,&rect,DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(item->hDC,old_font);
    if (item->itemState & ODS_FOCUS) {
        InflateRect(&rect,-gui_scale(app,4),-gui_scale(app,4)); DrawFocusRect(item->hDC,&rect);
    }
}
