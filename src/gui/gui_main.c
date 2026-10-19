#include "gui_app.h"
#include "storage.h"
#include <stdlib.h>

/* GUI 独立入口：不链接控制台 main/ui，正常启动先完成已有恢复和初始化。 */
/* 功能：独立 GUI 入口，参数为 Windows 实例/命令行/显示状态；正常退出 0，失败非零。 */
int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR command_line, int show)
{
    GuiApp app={0};
    wchar_t error[320];
    MSG message;
    int status;
    HANDLE mutex;
    (void)previous; (void)command_line;
    gui_enable_dpi();
    if (!gui_set_working_directory()) {
        MessageBoxW(NULL,L"无法将工作目录设置为程序所在目录，启动已停止。",L"LabSlot 启动失败",MB_ICONERROR);
        return EXIT_FAILURE;
    }
    /* 现有存储面向单进程；阻止重复 GUI 实例写同一项目数据。控制台仍须人工互斥运行。 */
    mutex=CreateMutexW(NULL,FALSE,L"Local\\LabSlot-GUI-P5.1");
    if (!mutex || GetLastError()==ERROR_ALREADY_EXISTS) {
        MessageBoxW(NULL,L"图形程序已在运行，或无法取得单实例保护。请使用已有窗口。",L"LabSlot",MB_ICONINFORMATION);
        if (mutex) { CloseHandle(mutex); }
        return EXIT_FAILURE;
    }
    if (!gui_initialize_data(error,320)) {
        MessageBoxW(NULL,error,L"LabSlot 启动失败",MB_ICONERROR);
        CloseHandle(mutex); return EXIT_FAILURE;
    }
    if (!gui_create(&app,instance)) {
        MessageBoxW(NULL,L"无法创建窗口或界面资源。",L"LabSlot 启动失败",MB_ICONERROR);
        CloseHandle(mutex); return EXIT_FAILURE;
    }
    ShowWindow(app.window,show); UpdateWindow(app.window);
    while ((status=GetMessageW(&message,NULL,0,0))>0) {
        /* Tab 使用原生对话框导航；Enter 在编辑框执行当前页主操作。 */
        if (!gui_translate_message(&app,&message)) {
            TranslateMessage(&message); DispatchMessageW(&message);
        }
    }
    if (IsWindow(app.window)) { DestroyWindow(app.window); }
    CloseHandle(mutex);
    return status<0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
