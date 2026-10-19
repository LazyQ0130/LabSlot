#include "auth.h"

#include <stdlib.h>
#include <string.h>
#include "storage.h"
#include "utils.h"

/* 功能：共享注册流程，账户只在持久化成功后对外可见。
 * 参数：role 角色；id/name/password 完整字段。返回：详细 AuthResult。
 */
static AuthResult register_account(UserRole role, const char *id, const char *name,
                                   const char *password)
{
    AccountRecords *records; /* 单次操作的堆上快照，不作为全局缓存。 */
    size_t count = 0; /* 原有条数。 */
    size_t i; /* 查重下标。 */
    size_t capacity = role == ROLE_ADMIN ? MAX_ADMINS : MAX_STUDENTS; /* 角色容量。 */
    AuthResult result = AUTH_STORAGE_ERROR; /* 默认失败，不提前报告成功。 */
    if (!utils_valid_id(id) || !utils_valid_field(name, NAME_LEN) ||
        !utils_valid_field(password, PASSWORD_LEN)) {
        return AUTH_INVALID_INPUT;
    }
    records = calloc(1, sizeof *records);
    if (records == NULL) {
        return AUTH_NO_MEMORY;
    }
    if (!storage_load_accounts(role, records, &count)) {
        goto done;
    }
    for (i = 0; i < count; ++i) {
        const char *existing = role == ROLE_ADMIN ? records->admins[i].id : records->students[i].id;
        if (strcmp(existing, id) == 0) {
            result = AUTH_DUPLICATE;
            goto done;
        }
    }
    if (count == capacity) {
        result = AUTH_CAPACITY;
        goto done;
    }
    if (role == ROLE_ADMIN) {
        Admin *account = &records->admins[count]; /* 候选新账户，只改局部快照。 */
        strcpy(account->id, id);
        strcpy(account->name, name);
        strcpy(account->password, password);
    } else {
        Student *account = &records->students[count]; /* 新学生不接受调用方提供的累计时间。 */
        strcpy(account->id, id);
        strcpy(account->name, name);
        strcpy(account->password, password);
        account->time = 0;
    }
    if (storage_save_accounts(role, records, count + 1)) {
        result = AUTH_OK;
    }
done:
    utils_clear_secret(records, sizeof *records);
    free(records);
    return result;
}

/* 功能：管理员注册；参数与返回值见 auth.h。 */
AuthResult auth_register_admin(const Admin *account)
{
    return account == NULL ? AUTH_INVALID_INPUT :
        register_account(ROLE_ADMIN, account->id, account->name, account->password);
}

/* 功能：学生注册；参数与返回值见 auth.h。 */
AuthResult auth_register_student(const Student *account)
{
    return account == NULL ? AUTH_INVALID_INPUT :
        register_account(ROLE_STUDENT, account->id, account->name, account->password);
}

/* 功能：按指定角色认证，每次重新读取账户文件；无旧会话可被失败尝试继承。
 * 参数：role 角色；id/password 凭据；session 输出。返回：详细结果码。
 */
static AuthResult login_account(UserRole role, const char *id, const char *password,
                                AuthSession *session)
{
    AccountRecords *records; /* 不长期保留密码或账户表。 */
    size_t count = 0; /* 账户条数。 */
    size_t i; /* 查找下标。 */
    AuthResult result = AUTH_NOT_FOUND; /* 只在成功读取完整集合后才允许此结果。 */
    auth_logout(session);
    if (session == NULL || !utils_valid_id(id) || !utils_valid_field(password, PASSWORD_LEN)) {
        return AUTH_INVALID_INPUT;
    }
    records = calloc(1, sizeof *records);
    if (records == NULL) {
        return AUTH_NO_MEMORY;
    }
    if (!storage_load_accounts(role, records, &count)) {
        result = AUTH_STORAGE_ERROR;
        goto done;
    }
    for (i = 0; i < count; ++i) {
        const char *existing = role == ROLE_ADMIN ? records->admins[i].id : records->students[i].id;
        const char *expected = role == ROLE_ADMIN ? records->admins[i].password : records->students[i].password;
        if (strcmp(existing, id) == 0) {
            result = strcmp(expected, password) == 0 ? AUTH_OK : AUTH_BAD_PASSWORD;
            break;
        }
    }
    if (result == AUTH_OK) {
        session->role = role;
        strcpy(session->user_id, id);
        session->logged_in = 1; /* 最后一步才发布认证成功状态。 */
    }
done:
    utils_clear_secret(records, sizeof *records);
    free(records);
    return result;
}

/* 功能：管理员登录；参数与返回值见 auth.h。 */
AuthResult auth_login_admin(const char *id, const char *password, AuthSession *session)
{
    return login_account(ROLE_ADMIN, id, password, session);
}

/* 功能：学生登录；参数与返回值见 auth.h。 */
AuthResult auth_login_student(const char *id, const char *password, AuthSession *session)
{
    return login_account(ROLE_STUDENT, id, password, session);
}

/* 功能：清除角色、标识和标志；参数与返回值见 auth.h。 */
void auth_logout(AuthSession *session)
{
    if (session != NULL) {
        memset(session, 0, sizeof *session);
    }
}

/* 功能：显式会话角色校验；参数与返回值见 auth.h。 */
int auth_session_has_role(const AuthSession *session, UserRole role)
{
    return session != NULL && session->logged_in == 1 && session->role == role &&
        (role == ROLE_ADMIN || role == ROLE_STUDENT) && utils_valid_id(session->user_id);
}

/* 功能：结果转换为不含账户原文的提示；参数与返回值见 auth.h。 */
const char *auth_result_message(AuthResult result)
{
    switch (result) {
    case AUTH_OK: return "操作成功";
    case AUTH_INVALID_INPUT: return "字段格式或长度无效";
    case AUTH_DUPLICATE: return "该角色的工号或学号已存在";
    case AUTH_NOT_FOUND: return "该角色的账户不存在";
    case AUTH_BAD_PASSWORD: return "密码错误";
    case AUTH_STORAGE_ERROR: return "账户文件读取、格式或写入错误，请检查上述文件提示";
    case AUTH_CAPACITY: return "账户数量已达到容量上限";
    case AUTH_NO_MEMORY: return "内存分配失败";
    }
    return "未知认证结果";
}

/* 功能：会话与真实账户检查，供 P3 共用；参数与返回值见 auth.h。 */
AuthResult auth_validate_session(const AuthSession *session, UserRole role)
{
    AccountRecords *records; /* 只在短暂操作内持有完整账户表。 */
    size_t count = 0, i; /* 数量与查找下标。 */
    AuthResult result = AUTH_NOT_FOUND; /* 必须成功加载后才能返回不存在。 */
    if (!auth_session_has_role(session, role)) { return AUTH_INVALID_INPUT; }
    records = malloc(sizeof *records);
    if (records == NULL) { return AUTH_NO_MEMORY; }
    if (!storage_load_accounts(role, records, &count)) { result = AUTH_STORAGE_ERROR; }
    else {
        for (i = 0; i < count; ++i) {
            const char *id = role == ROLE_ADMIN ? records->admins[i].id : records->students[i].id; /* 当前角色成员。 */
            if (strcmp(id, session->user_id) == 0) { result = AUTH_OK; break; }
        }
    }
    utils_clear_secret(records, sizeof *records); free(records);
    return result;
}
