#ifndef SMART_LAB_UI_H
#define SMART_LAB_UI_H

#include "model.h"

/* 功能：Windows 控制台设置 UTF-8，重定向及其他平台不改编码。参数：无。返回：无。 */
void ui_initialize_console(void);

/* 功能：打印 5×8 座位图。参数：computers 为已校验完整集合。返回：无。 */
void ui_print_seat_map(const Lab computers[COMPUTER_COUNT]);

/* 功能：主菜单、角色入口、注册登录和角色菜单；4/兼容 0/EOF 正常退出。
 * 参数：computers 为只读电脑集合。返回：正常 1，输入/输出错误 0。
 */
int ui_run(const Lab computers[COMPUTER_COUNT]);

#endif
