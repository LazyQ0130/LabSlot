#include "gui_app.h"
#include "gui_theme.h"
#include "gui_widgets.h"
#include "gui_login.h"
#include "gui_dashboard.h"
#include "gui_booking.h"
#include "utils.h"
#include "storage.h"
#include <limits.h>
#include <string.h>
#include <wchar.h>

/* 功能：按当前窗口 DPI 转换逻辑像素；参数 app/value；返回物理像素。 */
int gui_scale(const GuiApp *app, int value) { return MulDiv(value,(int)app->dpi,96); }
/* 功能：严格转换 UTF-16 输入到有界 UTF-8；参数输入/输出/字节容量；成功返回 1。 */
int gui_utf8(const wchar_t *text, char *output, size_t capacity)
{
    int needed;
    if (!text || !output || capacity==0 || capacity>INT_MAX) { return 0; }
    needed=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text,-1,NULL,0,NULL,NULL);
    if (needed<=0 || (size_t)needed>capacity) { return 0; }
    return WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text,-1,output,(int)capacity,NULL,NULL)==needed;
}
/* 功能：严格转换 UTF-8 业务消息到宽字符；参数输入/输出/字符容量；成功返回 1。 */
int gui_wide(const char *text, wchar_t *output, size_t capacity)
{
    int needed;
    if (!text || !output || capacity==0 || capacity>INT_MAX) { return 0; }
    output[0]=L'\0';
    needed=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text,-1,NULL,0);
    if (needed<=0 || (size_t)needed>capacity) { return 0; }
    return MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text,-1,output,(int)capacity)==needed;
}
/* 功能：保存无凭据的内联提示并重绘；参数应用/提示/错误标志；无返回。 */
void gui_notice(GuiApp *app, const wchar_t *text, int error)
{
    wcsncpy(app->notice,text,sizeof app->notice/sizeof app->notice[0]-1);
    app->notice[sizeof app->notice/sizeof app->notice[0]-1]=L'\0';
    app->notice_error=error; InvalidateRect(app->window,NULL,FALSE);
}
/* 功能：转换并显示业务提示；参数应用/UTF-8 提示/错误标志；无返回。 */
void gui_notice_utf8(GuiApp *app, const char *text, int error)
{
    wchar_t wide[320];
    if (!gui_wide(text,wide,320)) { wcscpy(wide,L"业务提示编码无效，请检查数据文件。立刻停止本次操作。"); }
    gui_notice(app,wide,error);
}
/* 功能：定位指定子控件；参数应用、编号及逻辑矩形；无返回。 */
void gui_move(GuiApp *app, int id, int x, int y, int width, int height)
{
    HWND control=GetDlgItem(app->window,id);
    if (control) { MoveWindow(control,gui_scale(app,x),gui_scale(app,y),gui_scale(app,width),gui_scale(app,height),TRUE); }
}
/* 功能：读取客户区并分派页面布局；参数应用；无返回。 */
void gui_layout(GuiApp *app)
{
    RECT rect;
    GetClientRect(app->window,&rect);
    app->width=MulDiv(rect.right,96,(int)app->dpi);
    app->height=MulDiv(rect.bottom,96,(int)app->dpi);
    if (app->page<=GUI_REGISTER) { gui_login_layout(app); }
    else {
        gui_shell_layout(app);
        if (app->page==GUI_SEATS) { gui_booking_layout(app); }
        else { gui_dashboard_layout(app); }
    }
    InvalidateRect(app->window,NULL,FALSE);
}
/* 功能：销毁旧控件、检查会话并建立目标页面；参数应用/页面；无返回。 */
void gui_show_page(GuiApp *app, GuiPage page)
{
    if (page>GUI_REGISTER && !auth_session_has_role(&app->session,app->session.role)) { page=GUI_LOGIN; }
    if (page<=GUI_REGISTER) { auth_logout(&app->session); }
    gui_clear_controls(app);
    app->page=page; app->notice[0]=L'\0'; app->selected=0; app->seats_valid=0;
    if (page<=GUI_REGISTER) { gui_login_create(app); }
    else {
        gui_shell_create(app);
        if (page==GUI_SEATS) { gui_booking_create(app); }
        else { gui_dashboard_create(app); }
    }
    gui_layout(app);
    SetFocus(GetDlgItem(app->window,page<=GUI_REGISTER ? GUI_ID : page==GUI_SEATS ? GUI_DATE : GUI_HOME));
}
/* 用动态符号支持旧系统回退，避免把新 DPI API 作为必要导入。 */
/* 功能：启用 PerMonitorV2，旧系统回退系统 DPI 感知；无参数/返回。 */
void gui_enable_dpi(void)
{
    typedef BOOL (WINAPI *AwarenessFn)(HANDLE);
    FARPROC symbol=GetProcAddress(GetModuleHandleW(L"user32.dll"),"SetProcessDpiAwarenessContext");
    AwarenessFn function=NULL;
    if (symbol) {
        _Static_assert(sizeof function==sizeof symbol,"Windows function pointer size");
        memcpy(&function,&symbol,sizeof function);
        if (function((HANDLE)(INT_PTR)-4)) { return; }
    }
    SetProcessDPIAware();
}
/* 功能：重建字体并布局；参数应用/新 DPI；资源失败关闭窗口，无返回。 */
void gui_update_dpi(GuiApp *app, UINT dpi)
{
    size_t i;
    if (dpi==0) { return; }
    app->dpi=dpi;
    gui_theme_destroy(app);
    if (!gui_theme_create(app)) {
        MessageBoxW(app->window,L"无法创建界面字体或绘图资源。",L"LabSlot",MB_ICONERROR);
        DestroyWindow(app->window); return;
    }
    for (i=0;i<app->child_count;++i) { SendMessageW(app->children[i],WM_SETFONT,(WPARAM)app->body,TRUE); }
    gui_layout(app);
}
/* 默认数据根目录明确固定为 EXE 所在目录，防止双击/快捷方式创建另一份 data。 */
/* 功能：将数据根目录明确设为 EXE 所在目录；无参数；成功返回 1。 */
int gui_set_working_directory(void)
{
    wchar_t path[32768], *slash;
    DWORD length=GetModuleFileNameW(NULL,path,32768);
    if (!length || length>=32768) { return 0; }
    slash=wcsrchr(path,L'\\');
    if (!slash) { return 0; }
    slash[1]=L'\0';
    return SetCurrentDirectoryW(path)!=0;
}
/* 功能：复用恢复和初始化并输出有界错误说明；参数错误缓冲/容量；成功返回 1。 */
int gui_initialize_data(wchar_t *error, size_t capacity)
{
    Lab computers[COMPUTER_COUNT];
    const wchar_t *text;
    if (!error || capacity==0) { return 0; }
    if (storage_recover_maintenance()!=STORAGE_OK) {
        text=L"维护日志无法恢复。正常业务已阻止；请完整备份 data/，检查维护日志、相关数据和未完成的 .tmp 文件后重试。不会重置数据。";
    } else if (!storage_initialize(computers)) {
        text=L"数据初始化失败：请检查程序目录下 data/ 的访问权限、电脑文件完整性与遗留快照。已有数据未被重置。";
    } else { error[0]=L'\0'; return 1; }
    wcsncpy(error,text,capacity-1); error[capacity-1]=L'\0'; return 0;
}
/* 功能：处理认证 Enter 和原生 Tab 导航；参数应用/消息；消费消息返回 1。 */
int gui_translate_message(GuiApp *app, MSG *message)
{
    if (message->message==WM_KEYDOWN && message->wParam==VK_RETURN &&
        app->page<=GUI_REGISTER && (message->hwnd==app->id || message->hwnd==app->name ||
        message->hwnd==app->password || message->hwnd==app->confirmation)) {
        SendMessageW(app->window,WM_COMMAND,MAKEWPARAM(GUI_SUBMIT,BN_CLICKED),0);
        return 1;
    }
    return IsDialogMessageW(app->window,message)!=0;
}
/* 功能：按页面和控件通知分发命令；参数应用/控件编号/通知码；无返回。 */
static void command(GuiApp *app, int id, int code)
{
    if (app->page<=GUI_REGISTER) {
        if (code==BN_CLICKED) { gui_login_command(app,id); }
        return;
    }
    if (app->busy) { return; }
    if (id==GUI_SLOT && code==CBN_SELCHANGE && app->page==GUI_SEATS) { gui_booking_refresh(app); }
    else if (code==BN_CLICKED) {
        if (id==GUI_LOGOUT) {
            auth_logout(&app->session); gui_show_page(app,GUI_LOGIN);
            gui_notice(app,L"已退出登录，会话已清除。",0);
        } else if (id==GUI_HOME) { gui_show_page(app,GUI_DASHBOARD); }
        else if (id==GUI_BOOKING || id==GUI_QUICK) { gui_show_page(app,GUI_SEATS); }
        else if (app->page==GUI_SEATS) { gui_booking_command(app,id); }
    }
}
/* 功能：处理顶层 Win32 消息；参数窗口/消息/wp/lp；返回对应 LRESULT。 */
static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wp, LPARAM lp)
{
    GuiApp *app=(GuiApp *)GetWindowLongPtrW(window,GWLP_USERDATA);
    if (message==WM_NCCREATE) {
        CREATESTRUCTW *create=(CREATESTRUCTW *)lp;
        app=create->lpCreateParams; app->window=window;
        SetWindowLongPtrW(window,GWLP_USERDATA,(LONG_PTR)app);
    }
    if (!app) { return DefWindowProcW(window,message,wp,lp); }
    switch (message) {
    case WM_CREATE: {
        typedef UINT (WINAPI *DpiFn)(HWND);
        FARPROC symbol=GetProcAddress(GetModuleHandleW(L"user32.dll"),"GetDpiForWindow");
        DpiFn function=NULL;
        if (symbol) {
            memcpy(&function,&symbol,sizeof function);
            { UINT dpi=function(window); if (dpi) { app->dpi=dpi; } }
        }
        if (!gui_theme_create(app)) { return -1; }
        gui_show_page(app,GUI_LOGIN); return 0;
    }
    case WM_COMMAND: command(app,LOWORD(wp),HIWORD(wp)); return 0;
    case WM_NOTIFY:
        if (((NMHDR *)lp)->idFrom==GUI_DATE && ((NMHDR *)lp)->code==DTN_DATETIMECHANGE &&
            app->page==GUI_SEATS && !app->busy) { gui_booking_refresh(app); }
        return 0;
    case WM_DRAWITEM: gui_draw_button(app,(const DRAWITEMSTRUCT *)lp); return TRUE;
    case WM_CTLCOLOREDIT:
        SetTextColor((HDC)wp,GUI_INK); SetBkColor((HDC)wp,RGB(255,255,255));
        return (LRESULT)app->white;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint;
        HDC dc=BeginPaint(window,&paint);
        gui_paint(app,dc); EndPaint(window,&paint); return 0;
    }
    case WM_SIZE:
        if (wp!=SIZE_MINIMIZED) { gui_layout(app); }
        return 0;
    case WM_GETMINMAXINFO: {
        MINMAXINFO *info=(MINMAXINFO *)lp;
        RECT rect={0,0,gui_scale(app,1000),gui_scale(app,650)};
        AdjustWindowRectEx(&rect,WS_OVERLAPPEDWINDOW,FALSE,0);
        info->ptMinTrackSize.x=rect.right-rect.left;
        info->ptMinTrackSize.y=rect.bottom-rect.top;
        return 0;
    }
    case WM_DPICHANGED: {
        RECT rect=*(RECT *)lp;
        gui_update_dpi(app,HIWORD(wp));
        if (IsWindow(window)) { SetWindowPos(window,NULL,rect.left,rect.top,rect.right-rect.left,
                                            rect.bottom-rect.top,SWP_NOZORDER | SWP_NOACTIVATE); }
        return 0;
    }
    case WM_DESTROY:
        auth_logout(&app->session); gui_clear_controls(app); gui_theme_destroy(app);
        PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(window,message,wp,lp);
}
/* 功能：注册窗口类并创建工作区内的主窗口；参数应用/实例；失败返回 NULL。 */
HWND gui_create(GuiApp *app, HINSTANCE instance)
{
    WNDCLASSEXW type={0};
    INITCOMMONCONTROLSEX controls={sizeof controls,ICC_DATE_CLASSES | ICC_STANDARD_CLASSES};
    RECT rect;
    HDC dc;
    HWND window;
    MONITORINFO monitor={0};
    app->instance=instance; app->role=ROLE_STUDENT;
    dc=GetDC(NULL); app->dpi=dc ? (UINT)GetDeviceCaps(dc,LOGPIXELSX) : 96;
    if (dc) { ReleaseDC(NULL,dc); }
    type.cbSize=sizeof type; type.lpfnWndProc=window_proc; type.hInstance=instance;
    type.hCursor=LoadCursorW(NULL,IDC_ARROW); type.hIcon=LoadIconW(NULL,IDI_APPLICATION);
    type.lpszClassName=L"LabSlotWindow";
    if (!InitCommonControlsEx(&controls) || (!RegisterClassExW(&type) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)) { return NULL; }
    rect.left=0; rect.top=0; rect.right=gui_scale(app,1280); rect.bottom=gui_scale(app,800);
    AdjustWindowRectEx(&rect,WS_OVERLAPPEDWINDOW,FALSE,0);
    window=CreateWindowExW(WS_EX_CONTROLPARENT,type.lpszClassName,L"LabSlot · 智能实验室预约系统",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,rect.right-rect.left,
        rect.bottom-rect.top,NULL,NULL,instance,app);
    if (!window) { return NULL; }
    /* 按实际所在显示器 DPI 和工作区确定初始尺寸，避免 150% 时越出屏幕。 */
    monitor.cbSize=sizeof monitor;
    if (GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&monitor)) {
        int width,height,available_width=monitor.rcWork.right-monitor.rcWork.left;
        int available_height=monitor.rcWork.bottom-monitor.rcWork.top;
        rect.left=rect.top=0; rect.right=gui_scale(app,1280); rect.bottom=gui_scale(app,800);
        AdjustWindowRectEx(&rect,WS_OVERLAPPEDWINDOW,FALSE,0);
        width=rect.right-rect.left; height=rect.bottom-rect.top;
        if (width>available_width) { width=available_width; }
        if (height>available_height) { height=available_height; }
        SetWindowPos(window,NULL,monitor.rcWork.left+(available_width-width)/2,
                     monitor.rcWork.top+(available_height-height)/2,width,height,SWP_NOZORDER | SWP_NOACTIVATE);
    }
    return window;
}
