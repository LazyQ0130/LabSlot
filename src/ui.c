#include "ui.h"

#include <stdio.h>
#include <string.h>
#include "auth.h"
#include "lab.h"
#include "utils.h"

#ifdef _WIN32
#include <windows.h>
#endif

/* 功能：只调整实际 Windows 控制台的代码页；参数：无；返回：无。 */
void ui_initialize_console(void)
{
#ifdef _WIN32
    DWORD mode; /* 判断标准句柄是否连接控制台，而非重定向文件/管道。 */
    if (GetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), &mode) &&
        !SetConsoleOutputCP(CP_UTF8)) {
        fputs("警告：设置控制台 UTF-8 输出失败。\n", stderr);
    }
    if (GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), &mode) &&
        !SetConsoleCP(CP_UTF8)) {
        fputs("警告：设置控制台 UTF-8 输入失败。\n", stderr);
    }
#endif
}

/* 功能：使用等宽 ASCII 单元格打印座位；参数及返回值见 ui.h。 */
void ui_print_seat_map(const Lab computers[COMPUTER_COUNT])
{
    int row; /* 座位图行号。 */
    int col; /* 座位图列号。 */
    puts("\n实验室座位图（5 排 × 8 列）");
    puts("* 正常/空闲    ! 故障    # 已预约（P2 尚未开放）");
    for (row = 0; row < LAB_ROWS; ++row) {
        for (col = 0; col < LAB_COLUMNS; ++col) {
            const Lab *seat = &computers[row * LAB_COLUMNS + col]; /* 当前座位。 */
            printf("%02d[%c]%s", seat->no, lab_seat_symbol(seat),
                   col + 1 == LAB_COLUMNS ? "\n" : " ");
        }
    }
    puts("");
}

/* 功能：读取菜单并重试非法行。参数：maximum 上界；choice 输出。返回：InputResult。 */
static InputResult read_menu(int maximum, int *choice)
{
    for (;;) {
        InputResult result = utils_read_int_range("请输入选项：", 0, maximum, choice); /* 当前输入。 */
        if (result != INPUT_INVALID) {
            return result;
        }
        printf("输入无效，请输入 0～%d 的整数（单行不超过 127 字节）。\n", maximum);
    }
}

/* 功能：收集注册字段和密码确认，失败不创建账户。
 * 参数：role 目标角色。返回：INPUT_OK/INVALID 回入口，EOF/ERROR 传播至主菜单。
 */
static InputResult register_user(UserRole role)
{
    Student draft = {0}; /* 复用相同字段布局的学生候选，time 默认 0。 */
    Admin admin = {0}; /* 管理员注册时显式复制字段，不进行结构体强转。 */
    char confirmation[PASSWORD_LEN] = {0}; /* 二次确认，仅短暂存在。 */
    InputResult input; /* 任意字段失败立即取消，不使用截断内容。 */
    AuthResult result; /* 认证模块结果，只有 AUTH_OK 才报成功。 */
    input = utils_read_field(role == ROLE_ADMIN ? "工号：" : "学号：", draft.id, sizeof draft.id, 0);
    if (input != INPUT_OK) {
        goto done;
    }
    if (!utils_valid_id(draft.id)) {
        input = INPUT_INVALID;
        goto done;
    }
    input = utils_read_field("姓名：", draft.name, sizeof draft.name, 0);
    if (input != INPUT_OK) {
        goto done;
    }
    input = utils_read_field("密码：", draft.password, sizeof draft.password, 1);
    if (input != INPUT_OK) {
        goto done;
    }
    input = utils_read_field("再次输入密码：", confirmation, sizeof confirmation, 1);
    if (input != INPUT_OK) {
        goto done;
    }
    if (strcmp(draft.password, confirmation) != 0) {
        puts("两次密码不一致，未创建账户。");
        goto done;
    }
    if (role == ROLE_ADMIN) {
        strcpy(admin.id, draft.id);
        strcpy(admin.name, draft.name);
        strcpy(admin.password, draft.password);
        result = auth_register_admin(&admin);
    } else {
        result = auth_register_student(&draft);
    }
    if (result == AUTH_OK) {
        puts("注册成功，请选择登录。");
    } else {
        printf("注册失败：%s。\n", auth_result_message(result));
    }
done:
    if (input == INPUT_INVALID) {
        puts("输入无效：字段不能为空、超长或含非法字符；本次注册已取消。");
    }
    utils_clear_secret(&draft, sizeof draft);
    utils_clear_secret(&admin, sizeof admin);
    utils_clear_secret(confirmation, sizeof confirmation);
    return input;
}

/* 功能：显示已认证角色菜单，进入及每轮均检查同一角色权限。
 * 参数：role 预期角色；session 当前会话；computers 座位。返回：InputResult。
 */
static InputResult user_menu(UserRole role, AuthSession *session,
                             const Lab computers[COMPUTER_COUNT])
{
    for (;;) {
        int choice; /* 当前角色菜单选项。 */
        InputResult input; /* EOF/ERROR 必须退出并清除会话。 */
        if (!auth_session_has_role(session, role)) {
            fputs("访问被拒绝：需要对应角色登录。\n", stderr);
            auth_logout(session);
            return INPUT_ERROR;
        }
        printf("\n------ %s菜单 ------\n当前%s：%s\n",
               role == ROLE_ADMIN ? "管理员" : "学生",
               role == ROLE_ADMIN ? "工号" : "学号", session->user_id);
        puts("1. 查看实验室座位");
        if (role == ROLE_ADMIN) {
            puts("2. 电脑状态维护（P3）\n3. 查询实验室使用情况（P3）");
            puts("4. 查询学生信息（P3）\n5. 统计实验室使用情况（P4）\n6. 退出登录");
        } else {
            puts("2. 预约电脑（P2）\n3. 查询我的预约（P2）\n4. 取消预约（P2）");
            puts("5. 报告电脑故障（P3）\n6. 查看累计实验时间（P4）\n7. 退出登录");
        }
        input = read_menu(role == ROLE_ADMIN ? 6 : 7, &choice);
        if (input != INPUT_OK) {
            auth_logout(session);
            return input;
        }
        if (choice == 0 || choice == (role == ROLE_ADMIN ? 6 : 7)) {
            auth_logout(session);
            puts("已退出登录，登录状态已清除。");
            return INPUT_OK;
        }
        if (choice == 1) {
            ui_print_seat_map(computers);
        } else {
            puts("该功能尚未开放");
        }
    }
}

/* 功能：收集角色凭据并在成功后进入角色菜单。
 * 参数：role 角色；session 输出；computers 座位。返回：InputResult。
 */
static InputResult login_user(UserRole role, AuthSession *session,
                              const Lab computers[COMPUTER_COUNT])
{
    char id[ID_LEN] = {0}; /* 本次登录标识。 */
    char password[PASSWORD_LEN] = {0}; /* 本次登录密码，不传给会话。 */
    InputResult input; /* 字段输入结果。 */
    AuthResult result; /* 登录结果。 */
    auth_logout(session);
    input = utils_read_field(role == ROLE_ADMIN ? "工号：" : "学号：", id, sizeof id, 0);
    if (input != INPUT_OK) {
        goto done;
    }
    if (!utils_valid_id(id)) {
        input = INPUT_INVALID;
        goto done;
    }
    input = utils_read_field("密码：", password, sizeof password, 1);
    if (input != INPUT_OK) {
        goto done;
    }
    result = role == ROLE_ADMIN ? auth_login_admin(id, password, session) :
                                 auth_login_student(id, password, session);
    /* 在进入下一菜单前就擦除密码，避免它在会话期间仍留在此栈帧。 */
    utils_clear_secret(password, sizeof password);
    if (result == AUTH_OK) {
        puts("登录成功。");
        input = user_menu(role, session, computers);
    } else {
        printf("登录失败：%s。可重新选择登录或返回。\n", auth_result_message(result));
    }
done:
    if (input == INPUT_INVALID) {
        puts("输入无效：账户标识或密码格式/长度不合法，本次登录已取消。");
    }
    utils_clear_secret(password, sizeof password);
    auth_logout(session); /* 离开登录流程时绝不遗留会话，包括 EOF。 */
    return input;
}

/* 功能：角色入口只允许注册、登录和返回，不直接访问已登录菜单。
 * 参数：role 固定角色；session 调用方持有的会话；computers 座位。返回：InputResult。
 */
static InputResult role_entry(UserRole role, AuthSession *session,
                              const Lab computers[COMPUTER_COUNT])
{
    auth_logout(session);
    for (;;) {
        int choice; /* 本次入口选择。 */
        InputResult input; /* 子流程结果。 */
        printf("\n------ %s入口 ------\n", role == ROLE_ADMIN ? "管理员" : "学生");
        printf("1. %s注册\n2. %s登录\n3. 返回主菜单\n",
               role == ROLE_ADMIN ? "管理员" : "学生", role == ROLE_ADMIN ? "管理员" : "学生");
        input = read_menu(3, &choice);
        if (input != INPUT_OK) {
            return input;
        }
        if (choice == 0 || choice == 3) {
            auth_logout(session);
            return INPUT_OK;
        }
        input = choice == 1 ? register_user(role) : login_user(role, session, computers);
        if (input == INPUT_EOF || input == INPUT_ERROR) {
            auth_logout(session);
            return input;
        }
    }
}

/* 功能：主菜单持有显式会话，所有退出路径统一清理；参数、返回值见 ui.h。 */
int ui_run(const Lab computers[COMPUTER_COUNT])
{
    AuthSession session = {0}; /* 无全局会话，不能靠切菜单继承权限。 */
    InputResult input = INPUT_OK; /* 用于统一 EOF/错误退出。 */
    for (;;) {
        int choice; /* 主菜单选项。 */
        puts("========================================");
        puts("         智能实验室预约系统");
        puts("========================================\n");
        puts("        1. 管理员入口");
        puts("        2. 学生入口");
        puts("        3. 查看实验室座位图");
        puts("        4. 退出系统（兼容输入 0）\n");
        puts("========================================");
        input = read_menu(4, &choice);
        if (input != INPUT_OK) {
            break;
        }
        if (choice == 0 || choice == 4) {
            puts("感谢使用，再见！");
            break;
        }
        if (choice == 3) {
            ui_print_seat_map(computers);
        } else {
            input = role_entry(choice == 1 ? ROLE_ADMIN : ROLE_STUDENT, &session, computers);
        }
        if (input == INPUT_EOF || input == INPUT_ERROR) {
            break;
        }
    }
    auth_logout(&session);
    if (input == INPUT_EOF) {
        puts("\n输入结束，退出系统。");
    } else if (input == INPUT_ERROR) {
        fputs("输入或输出流错误，程序退出。\n", stderr);
        return 0;
    }
    return 1;
}
