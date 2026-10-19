#include "gui_login.h"
#include "gui_widgets.h"
#include "gui_theme.h"
#include "storage.h"
#include "utils.h"
#include <string.h>

/* 功能：建立当前认证页原生控件；参数应用；无返回。 */
void gui_login_create(GuiApp *app)
{
    gui_button(app,L"学生",GUI_STUDENT); gui_button(app,L"管理员",GUI_ADMIN);
    app->id=gui_edit(app,GUI_ID,0,ID_LEN-1);
    if (app->page==GUI_REGISTER) { app->name=gui_edit(app,GUI_NAME,0,NAME_LEN-1); }
    app->password=gui_edit(app,GUI_PASSWORD,1,PASSWORD_LEN-1);
    if (app->page==GUI_REGISTER) { app->confirmation=gui_edit(app,GUI_CONFIRM,1,PASSWORD_LEN-1); }
    gui_button(app,app->page==GUI_REGISTER ? L"创建账户" : L"登录 LabSlot",GUI_SUBMIT);
    gui_button(app,app->page==GUI_REGISTER ? L"已有账户？返回登录" : L"还没有账户？立即注册",GUI_SWITCH);
}
/* 功能：按当前客户区定位认证控件；参数应用；无返回。 */
void gui_login_layout(GuiApp *app)
{
    int x=app->width/2+24, width=app->width/2-88;
    int top=(app->height-610)/2, y=top+150;
    gui_move(app,GUI_STUDENT,x+28,top+90,(width-64)/2,40);
    gui_move(app,GUI_ADMIN,x+36+(width-64)/2,top+90,(width-64)/2,40);
    gui_move(app,GUI_ID,x+28,y+24,width-56,38); y+=68;
    if (app->page==GUI_REGISTER) { gui_move(app,GUI_NAME,x+28,y+24,width-56,38); y+=68; }
    gui_move(app,GUI_PASSWORD,x+28,y+24,width-56,38); y+=68;
    if (app->page==GUI_REGISTER) { gui_move(app,GUI_CONFIRM,x+28,y+24,width-56,38); y+=68; }
    gui_move(app,GUI_SUBMIT,x+28,y+10,width-56,44);
    gui_move(app,GUI_SWITCH,x+28,y+66,width-56,36);
}
/* 功能：绘制认证标题、标签及内联说明；参数应用/DC；无返回。 */
void gui_login_paint(GuiApp *app, HDC dc)
{
    int x=app->width/2+24, width=app->width/2-88;
    int top=(app->height-610)/2, y=top+150;
    gui_card(app,dc,56,top+60,64,64,GUI_BLUE);
    gui_text(app,dc,L"L",56,top+63,64,58,app->hero,RGB(255,255,255),DT_CENTER | DT_SINGLELINE);
    gui_text(app,dc,L"LabSlot",56,top+150,app->width/2-90,60,app->hero,GUI_INK,DT_SINGLELINE);
    gui_text(app,dc,L"让每一次探索，\n都有一席之地。",56,top+228,app->width/2-90,112,
             app->heading,GUI_INK,DT_WORDBREAK);
    gui_text(app,dc,L"智能实验室预约系统",56,top+364,app->width/2-90,28,app->body,GUI_MUTED,0);
    gui_text(app,dc,L"40 台电脑  /  9 个时段  /  清晰可见",56,top+404,app->width/2-90,32,
             app->small,GUI_MUTED,0);
    gui_text(app,dc,L"校园实验室 · 学习与实践空间",56,app->height-48,app->width/2-90,24,
             app->small,GUI_MUTED,0);
    gui_card(app,dc,x,top,width,610,RGB(255,255,255));
    gui_text(app,dc,app->page==GUI_REGISTER ? L"创建你的账户" : L"欢迎回来",x+28,top+22,
             width-56,38,app->heading,GUI_INK,0);
    gui_text(app,dc,L"请选择身份，使用工号或学号继续",x+28,top+64,width-56,22,app->small,GUI_MUTED,0);
    gui_text(app,dc,app->role==ROLE_ADMIN ? L"工号" : L"学号",x+28,y,width-56,23,app->body,GUI_INK,0); y+=68;
    if (app->page==GUI_REGISTER) {
        gui_text(app,dc,L"姓名",x+28,y,width-56,23,app->body,GUI_INK,0); y+=68;
    }
    gui_text(app,dc,L"密码",x+28,y,width-56,23,app->body,GUI_INK,0); y+=68;
    if (app->page==GUI_REGISTER) {
        gui_text(app,dc,L"确认密码",x+28,y,width-56,23,app->body,GUI_INK,0); y+=68;
    }
    if (app->notice[0]) {
        gui_text(app,dc,app->notice,x+28,y+116,width-56,64,app->small,
                 app->notice_error ? GUI_RED : RGB(22,128,61),DT_WORDBREAK);
    } else if (app->page==GUI_REGISTER) {
        gui_text(app,dc,L"姓名、密码最多 63 个 UTF-8 字节。\n标识仅支持字母、数字、下划线和连字符。",
                 x+28,y+116,width-56,72,app->small,GUI_MUTED,DT_WORDBREAK);
    }
}
/* 字段获取与转换均有界；业务格式、查重和密码验证仍在 auth 中执行。 */
/* 功能：获取并清除短期凭据，调用原有认证 API；参数应用；无返回。 */
static void submit(GuiApp *app)
{
    Student student={0};
    Admin admin={0};
    char confirmation[PASSWORD_LEN]={0};
    AuthResult result=AUTH_INVALID_INPUT;
    int registering=app->page==GUI_REGISTER;
    if (app->busy) { return; }
    app->busy=1; EnableWindow(GetDlgItem(app->window,GUI_SUBMIT),FALSE);
    if (!storage_writes_allowed()) {
        gui_notice(app,L"维护恢复未完成：请关闭程序，检查 data/ 中的维护日志和文件后重新启动。",1);
        goto done;
    }
    if (!gui_read_edit(app->id,student.id,sizeof student.id) ||
        !gui_read_edit(app->password,student.password,sizeof student.password) ||
        !utils_valid_id(student.id) || !utils_valid_field(student.password,PASSWORD_LEN) ||
        (registering && (!gui_read_edit(app->name,student.name,sizeof student.name) ||
                        !utils_valid_field(student.name,NAME_LEN) ||
                        !gui_read_edit(app->confirmation,confirmation,sizeof confirmation)))) {
        gui_notice(app,L"输入无效：请检查长度、UTF-8 编码和字段格式；禁止空值、首尾空格、控制符及 |。标识最多 31 字节，姓名、密码最多 63 字节。",1);
        goto done;
    }
    if (registering && strcmp(student.password,confirmation)!=0) {
        gui_notice(app,L"两次输入的密码不一致，请重新确认。",1); goto done;
    }
    if (registering) {
        if (app->role==ROLE_ADMIN) {
            strcpy(admin.id,student.id); strcpy(admin.name,student.name); strcpy(admin.password,student.password);
            result=auth_register_admin(&admin);
        } else { result=auth_register_student(&student); }
    } else {
        result=app->role==ROLE_ADMIN ? auth_login_admin(student.id,student.password,&app->session) :
                                      auth_login_student(student.id,student.password,&app->session);
    }
    if (result==AUTH_OK) {
        gui_show_page(app,registering ? GUI_LOGIN : GUI_DASHBOARD);
        if (registering) { gui_notice(app,L"注册成功，请登录。",0); }
    } else {
        if (result==AUTH_STORAGE_ERROR) {
            gui_notice(app,L"账户文件无法读取、格式损坏或保存失败。请检查当前角色账户文件及遗留 .tmp 文件；程序未重置数据。",1);
        } else { gui_notice_utf8(app,auth_result_message(result),1); }
    }
done:
    if (app->password) { SetWindowTextW(app->password,L""); }
    if (app->confirmation) { SetWindowTextW(app->confirmation,L""); }
    utils_clear_secret(&student,sizeof student); utils_clear_secret(&admin,sizeof admin);
    utils_clear_secret(confirmation,sizeof confirmation);
    app->busy=0; EnableWindow(GetDlgItem(app->window,GUI_SUBMIT),TRUE);
}
/* 功能：处理认证页角色/页面/提交事件；参数应用/控件编号；无返回。 */
void gui_login_command(GuiApp *app, int id)
{
    if (app->busy) { return; }
    if (id==GUI_SUBMIT) { submit(app); }
    else if (id==GUI_SWITCH) { gui_show_page(app,app->page==GUI_REGISTER ? GUI_LOGIN : GUI_REGISTER); }
    else if (id==GUI_STUDENT || id==GUI_ADMIN) {
        app->role=id==GUI_ADMIN ? ROLE_ADMIN : ROLE_STUDENT;
        gui_show_page(app,app->page);
    }
}
