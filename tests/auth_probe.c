/* P1 API 集成测试。只输出结果码和失败行号，不输出密码或账户原文。 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "auth.h"
#include "storage.h"
#include "utils.h"
#ifdef _WIN32
#include <windows.h>
#endif

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "API check failed at line %d\n", __LINE__); return EXIT_FAILURE; \
} } while (0)

/* 以下仅为隔离测试账户的固定合成凭据，从不打印。 */
static const char test_password[] = "CourseOnly123!";

/* 功能：创建测试账户。参数：role 角色；id 标识。返回：AuthResult。 */
static AuthResult register_test(UserRole role, const char *id)
{
    Admin admin = {0}; /* 管理员测试候选。 */
    Student student = {0}; /* 学生测试候选。 */
    AuthResult result; /* 实际调用结果。 */
    strcpy(admin.id, id); strcpy(admin.name, "Test Name"); strcpy(admin.password, test_password);
    strcpy(student.id, id); strcpy(student.name, "Test Name"); strcpy(student.password, test_password);
    student.time = 999; /* 模块必须忽略此值，使新学生 time=0。 */
    result = role == ROLE_ADMIN ? auth_register_admin(&admin) : auth_register_student(&student);
    utils_clear_secret(&admin, sizeof admin);
    utils_clear_secret(&student, sizeof student);
    return result;
}

/* 功能：按角色调用登录。参数：role/id/password 输入；session 输出。返回：AuthResult。 */
static AuthResult login_test(UserRole role, const char *id, const char *password, AuthSession *session)
{
    return role == ROLE_ADMIN ? auth_login_admin(id, password, session) :
                                auth_login_student(id, password, session);
}

/* 功能：逐字节检查退出后所有会话字段归零。参数：session 输入。返回：全零 1，否则 0。 */
static int session_empty(const AuthSession *session)
{
    AuthSession empty; /* 作为包括填充字节在内的全零基准。 */
    memset(&empty, 0, sizeof empty);
    return memcmp(session, &empty, sizeof empty) == 0;
}

/* 功能：执行指定角色/模式的 API 验证。
 * 参数：argv[1] 为 admin/student；argv[2] 模式。返回：成功 0，失败非零。
 */
int main(int argc, char **argv)
{
    Lab computers[COMPUTER_COUNT]; /* 初始化测试目录需要的电脑。 */
    UserRole role; /* 本进程测试的角色。 */
    AuthSession session = {0}; /* 被直接检查的会话。 */
    const char *path; /* 当前账户文件路径。 */
    const char *temporary; /* 对应临时路径。 */
    const char *mode; /* 测试模式。 */
    AuthResult result; /* 实际认证结果。 */
    CHECK(argc == 3);
    CHECK(strcmp(argv[1], "admin") == 0 || strcmp(argv[1], "student") == 0);
    role = strcmp(argv[1], "admin") == 0 ? ROLE_ADMIN : ROLE_STUDENT;
    path = role == ROLE_ADMIN ? ADMINS_PATH : STUDENTS_PATH;
    temporary = role == ROLE_ADMIN ? ADMINS_TEMP_PATH : STUDENTS_TEMP_PATH;
    mode = argv[2];
    CHECK(storage_initialize(computers));
    if (strcmp(mode, "roundtrip") == 0) {
        AccountRecords *records = calloc(1, sizeof *records); /* 堆上读取结果。 */
        size_t count = 0; /* 读取条数。 */
        CHECK(records != NULL);
        CHECK(register_test(role, "User1") == AUTH_OK);
        CHECK(register_test(role, "User2") == AUTH_OK);
        CHECK(register_test(role, "User1") == AUTH_DUPLICATE);
        CHECK(storage_load_accounts(role, records, &count) && count == 2);
        if (role == ROLE_STUDENT) {
            CHECK(records->students[0].time == 0 && records->students[1].time == 0);
        }
        utils_clear_secret(records, sizeof *records); free(records);
        CHECK(login_test(role, "User1", test_password, &session) == AUTH_OK);
        CHECK(auth_session_has_role(&session, role));
        CHECK(!auth_session_has_role(&session, role == ROLE_ADMIN ? ROLE_STUDENT : ROLE_ADMIN));
        CHECK(strcmp(session.user_id, "User1") == 0);
        CHECK(login_test(role, "User1", "WrongValue", &session) == AUTH_BAD_PASSWORD);
        CHECK(session_empty(&session));
        CHECK(login_test(role, "User2", test_password, &session) == AUTH_OK);
        CHECK(login_test(role, "Missing", test_password, &session) == AUTH_NOT_FOUND);
        CHECK(session_empty(&session));
        CHECK(login_test(role, "User1", test_password, &session) == AUTH_OK);
        CHECK(login_test(role, "bad|id", test_password, &session) == AUTH_INVALID_INPUT);
        CHECK(session_empty(&session));
        CHECK(login_test(role, "User1", test_password, &session) == AUTH_OK);
        auth_logout(&session);
        CHECK(session_empty(&session));
        CHECK(!auth_session_has_role(&session, role));
        session.role = role; strcpy(session.user_id, "User1"); session.logged_in = 0;
        CHECK(!auth_session_has_role(&session, role));
        session.logged_in = 1; session.user_id[0] = '\0';
        CHECK(!auth_session_has_role(&session, role));
        auth_logout(&session);
        CHECK(login_test(role, "User1", test_password, NULL) == AUTH_INVALID_INPUT);
        CHECK(!auth_session_has_role(NULL, role));
        CHECK(!auth_session_has_role(&session, ROLE_NONE));
        auth_logout(NULL);
    } else if (strcmp(mode, "capacity") == 0) {
        CHECK(register_test(role, "NewUser") == AUTH_CAPACITY);
        CHECK(register_test(role, "User0") == AUTH_DUPLICATE);
    } else if (strcmp(mode, "register") == 0) {
        result = register_test(role, "User1");
        printf("AUTH_RESULT=%d\n", (int)result);
        return result == AUTH_OK ? EXIT_SUCCESS : EXIT_FAILURE;
    } else if (strcmp(mode, "login") == 0) {
        CHECK(login_test(role, "User1", test_password, &session) == AUTH_OK);
        CHECK(auth_session_has_role(&session, role));
        auth_logout(&session); CHECK(session_empty(&session));
    } else if (strcmp(mode, "corrupt") == 0) {
        AccountRecords *records = malloc(sizeof *records); /* 保留输出哨兵。 */
        AccountRecords *before = malloc(sizeof *before); /* 比较基准。 */
        size_t count = 123; /* 出错不能被改成 0。 */
        CHECK(records != NULL && before != NULL);
        memset(records, 0xa5, sizeof *records); memcpy(before, records, sizeof *before);
        CHECK(!storage_load_accounts(role, records, &count));
        CHECK(count == 123 && memcmp(records, before, sizeof *records) == 0);
        free(records); free(before);
        session.logged_in = 1; session.role = role; strcpy(session.user_id, "Previous");
        CHECK(login_test(role, "User1", test_password, &session) == AUTH_STORAGE_ERROR);
        CHECK(session_empty(&session));
        CHECK(register_test(role, "User1") == AUTH_STORAGE_ERROR);
    } else if (strcmp(mode, "invalid-struct") == 0) {
        Admin admin = {0}; /* 字段未终止的直接 API 输入。 */
        Student student = {0}; /* 学生同样测试，不能依赖 UI 才安全。 */
        char unterminated[PASSWORD_LEN]; /* 未终止密码。 */
        memset(admin.id, 'A', sizeof admin.id); strcpy(admin.name, "Name"); strcpy(admin.password, test_password);
        memset(student.id, 'A', sizeof student.id); strcpy(student.name, "Name"); strcpy(student.password, test_password);
        CHECK(auth_register_admin(&admin) == AUTH_INVALID_INPUT);
        CHECK(auth_register_student(&student) == AUTH_INVALID_INPUT);
        strcpy(admin.id, "User1"); strcpy(student.id, "User1");
        memset(admin.name, 'A', sizeof admin.name); memset(student.name, 'A', sizeof student.name);
        CHECK(auth_register_admin(&admin) == AUTH_INVALID_INPUT);
        CHECK(auth_register_student(&student) == AUTH_INVALID_INPUT);
        strcpy(admin.name, "Name"); strcpy(student.name, "Name");
        memset(admin.password, 'A', sizeof admin.password); memset(student.password, 'A', sizeof student.password);
        CHECK(auth_register_admin(&admin) == AUTH_INVALID_INPUT);
        CHECK(auth_register_student(&student) == AUTH_INVALID_INPUT);
        memset(unterminated, 'X', sizeof unterminated);
        CHECK(login_test(role, "User1", unterminated, &session) == AUTH_INVALID_INPUT);
        CHECK(session_empty(&session));
        CHECK(auth_register_admin(NULL) == AUTH_INVALID_INPUT);
        CHECK(auth_register_student(NULL) == AUTH_INVALID_INPUT);
        utils_clear_secret(&admin, sizeof admin); utils_clear_secret(&student, sizeof student);
        utils_clear_secret(unterminated, sizeof unterminated);
    } else if (strcmp(mode, "read-missing") == 0) {
        const char *backup = role == ROLE_ADMIN ? "data/admins.saved" : "data/students.saved"; /* 单文件备份。 */
        CHECK(register_test(role, "User1") == AUTH_OK);
        CHECK(login_test(role, "User1", test_password, &session) == AUTH_OK);
        CHECK(rename(path, backup) == 0);
        CHECK(login_test(role, "User1", test_password, &session) == AUTH_STORAGE_ERROR);
        CHECK(session_empty(&session));
        CHECK(register_test(role, "User2") == AUTH_STORAGE_ERROR);
    } else if (strcmp(mode, "retry-write") == 0) {
        FILE *file = fopen(temporary, "wb"); /* 模拟上次失败留下的临时文件。 */
        const char *backup = role == ROLE_ADMIN ? "data/admins.tmp.saved" : "data/students.tmp.saved";
        CHECK(file != NULL && fputs("stale", file) >= 0 && fclose(file) == 0);
        CHECK(register_test(role, "User1") == AUTH_STORAGE_ERROR);
        CHECK(login_test(role, "User1", test_password, &session) == AUTH_NOT_FOUND);
        CHECK(session_empty(&session));
        CHECK(rename(temporary, backup) == 0); /* 明确单文件改名，不删除。 */
        CHECK(register_test(role, "User1") == AUTH_OK);
        CHECK(login_test(role, "User1", test_password, &session) == AUTH_OK);
        auth_logout(&session);
    } else if (strcmp(mode, "save-invalid") == 0) {
        AccountRecords *records = calloc(1, sizeof *records); /* 故意无效的待保存集合。 */
        size_t count = 0; /* 存储 API 输出。 */
        CHECK(records != NULL);
        CHECK(!storage_save_accounts(ROLE_NONE, records, 0));
        CHECK(!storage_save_accounts(role, NULL, 0));
        CHECK(!storage_load_accounts(role, NULL, &count));
        CHECK(!storage_load_accounts(role, records, NULL));
        CHECK(!storage_load_accounts(ROLE_NONE, records, &count));
        CHECK(!storage_save_accounts(role, records, (role == ROLE_ADMIN ? MAX_ADMINS : MAX_STUDENTS) + 1));
        CHECK(!storage_save_accounts(role, records, 1)); /* 空字段。 */
        if (role == ROLE_ADMIN) {
            strcpy(records->admins[0].id, "User1"); strcpy(records->admins[0].name, "Name");
            strcpy(records->admins[0].password, test_password); records->admins[1] = records->admins[0];
        } else {
            strcpy(records->students[0].id, "User1"); strcpy(records->students[0].name, "Name");
            strcpy(records->students[0].password, test_password); records->students[1] = records->students[0];
        }
        CHECK(!storage_save_accounts(role, records, 2)); /* 重复 ID。 */
        if (role == ROLE_STUDENT) {
            records->students[0].time = -1;
            CHECK(!storage_save_accounts(role, records, 1));
        }
        utils_clear_secret(records, sizeof *records); free(records);
#ifdef _WIN32
    } else if (strcmp(mode, "read-locked") == 0) {
        HANDLE locked; /* 禁止读共享，模拟实际读取失败。 */
        CHECK(register_test(role, "User1") == AUTH_OK);
        CHECK(login_test(role, "User1", test_password, &session) == AUTH_OK);
        locked = CreateFileA(path, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        CHECK(locked != INVALID_HANDLE_VALUE);
        CHECK(login_test(role, "User1", test_password, &session) == AUTH_STORAGE_ERROR);
        CHECK(session_empty(&session));
        CHECK(register_test(role, "User2") == AUTH_STORAGE_ERROR);
        CHECK(CloseHandle(locked));
#endif
    } else {
        CHECK(0);
    }
    puts("API checks OK");
    return EXIT_SUCCESS;
}
