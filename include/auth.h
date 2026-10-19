#ifndef SMART_LAB_AUTH_H
#define SMART_LAB_AUTH_H

#include "model.h"

/* 认证结果；禁止将非零值统一视为成功，I/O/损坏不能冒充账户不存在。 */
typedef enum {
    AUTH_OK = 0,         /* 操作成功。 */
    AUTH_INVALID_INPUT,  /* 字段格式或参数无效。 */
    AUTH_DUPLICATE,      /* 当前角色标识重复。 */
    AUTH_NOT_FOUND,      /* 当前角色没有该账户。 */
    AUTH_BAD_PASSWORD,   /* 密码不匹配。 */
    AUTH_STORAGE_ERROR,  /* 文件读取、损坏或写入失败。 */
    AUTH_CAPACITY,       /* 当前角色账户容量已满。 */
    AUTH_NO_MEMORY       /* 分配失败。 */
} AuthResult;

/* 显式登录会话，不含密码。调用方持有；退出、失败和 EOF 时清空。 */
typedef struct {
    UserRole role;       /* 当前角色，无会话为 ROLE_NONE。 */
    char user_id[ID_LEN]; /* 当前工号或学号。 */
    int logged_in;       /* 仅 1 表示已认证。 */
} AuthSession;

/* 功能：验证并注册管理员，重读原文件、查重、保存成功才返回 AUTH_OK。
 * 参数：account 完整待注册账户。返回：AuthResult；失败不更改原账户文件。
 */
AuthResult auth_register_admin(const Admin *account);

/* 功能：注册学生，初始累计时间强制为 0，其余保证同管理员注册。
 * 参数：account 待注册账户，输入 time 不被采纳。返回：AuthResult。
 */
AuthResult auth_register_student(const Student *account);

/* 功能：仅从管理员文件认证，成功创建会话，任何失败先清空旧会话。
 * 参数：id 工号；password 密码；session 输出会话。返回：AuthResult。
 */
AuthResult auth_login_admin(const char *id, const char *password, AuthSession *session);

/* 功能：仅从学生文件认证，成功创建会话，失败清空旧会话。
 * 参数：id 学号；password 密码；session 输出会话。返回：AuthResult。
 */
AuthResult auth_login_student(const char *id, const char *password, AuthSession *session);

/* 功能：清除全部会话字段。参数：session 可为 NULL。返回：无。 */
void auth_logout(AuthSession *session);

/* 功能：验证已登录、标识有效且角色匹配，供 UI/P2 权限入口复用。
 * 参数：session 会话；role 要求角色。返回：满足 1，否则 0。
 */
int auth_session_has_role(const AuthSession *session, UserRole role);

/* 功能：不含凭据的结果说明。参数：result 结果码。返回：静态只读文本。 */
const char *auth_result_message(AuthResult result);

/* 功能：检查角色会话并重新确认对应账户仍存在，不返回密码。
 * 参数：session/role 要求身份。返回：AUTH_OK、INVALID_INPUT、NOT_FOUND、STORAGE_ERROR 或 NO_MEMORY。
 */
AuthResult auth_validate_session(const AuthSession *session, UserRole role);

#endif
