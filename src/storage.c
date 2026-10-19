#include "storage.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include "lab.h"
#include "utils.h"

#ifdef _WIN32
#include <direct.h>
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#else
#include <sys/types.h>
#endif

/* 功能：报告带路径的系统错误。参数：action 操作；path 路径；返回：无。 */
static void report_file_error(const char *action, const char *path)
{
    int error_code = errno; /* 先保存 errno，避免输出操作改变原始原因。 */
    fprintf(stderr, "%s '%s' 失败：%s\n", action, path, strerror(error_code));
}

/* 功能：检查路径类型。参数：path 路径；want_directory 非零要求目录，否则普通文件。
 * 返回：符合类型 1，不存在 0，类型错误或查询失败 -1。
 */
static int check_path(const char *path, int want_directory)
{
#ifdef _WIN32
    struct _stat info; /* Windows 文件类型信息。 */
    if (_stat(path, &info) != 0) {
#else
    struct stat info; /* POSIX 文件类型信息。 */
    if (stat(path, &info) != 0) {
#endif
        if (errno == ENOENT) {
            return 0;
        }
        report_file_error("检查路径", path);
        return -1;
    }
#ifdef _WIN32
    if ((info.st_mode & _S_IFMT) != (want_directory ? _S_IFDIR : _S_IFREG)) {
#else
    if (want_directory ? !S_ISDIR(info.st_mode) : !S_ISREG(info.st_mode)) {
#endif
        fprintf(stderr, "路径 '%s' 应为%s。\n", path,
                want_directory ? "目录" : "普通文件");
        return -1;
    }
    return 1;
}

/* 功能：确认数据目录，缺失时创建。参数：无。返回：成功 1，失败 0。 */
static int ensure_data_directory(void)
{
    int exists = check_path(DATA_DIRECTORY, 1); /* 不能把同名普通文件当目录。 */
    if (exists != 0) {
        return exists == 1;
    }
#ifdef _WIN32
    if (_mkdir(DATA_DIRECTORY) != 0) {
#else
    if (mkdir(DATA_DIRECTORY, 0755) != 0) {
#endif
        report_file_error("创建目录", DATA_DIRECTORY);
        return 0;
    }
    return 1;
}

/* 功能：关闭输入流并检查关闭结果。参数：file 已打开的流；path 诊断路径。
 * 返回：关闭成功 1，失败 0；无论结果如何，调用方不得再次使用 file。
 */
static int close_input(FILE *file, const char *path)
{
    if (fclose(file) != 0) {
        report_file_error("关闭文件", path);
        return 0;
    }
    return 1;
}

/* 功能：创建缺失的后续业务文件，已有文件只读并验证 I/O，不解析记录。
 * 参数：path 为指定文件路径。返回：成功 1，失败 0。
 */
static int ensure_readable_file(const char *path)
{
    int exists = check_path(path, 0); /* 区分文件缺失和权限/类型错误。 */
    FILE *file; /* 当前打开的文件流。 */
    unsigned char buffer[256]; /* 只验证字节可读，不改变尚未实现的业务记录。 */
    int success = 1; /* 聚合读取和关闭错误。 */
    if (exists < 0) {
        return 0;
    }
    /* ab 只用于创建缺失文件，绝不截断已有文件；正常情况下不会追加任何字节。 */
    file = fopen(path, exists ? "rb" : "ab");
    if (file == NULL) {
        report_file_error(exists ? "读取文件" : "创建文件", path);
        return 0;
    }
    if (exists) {
        while (fread(buffer, 1, sizeof buffer, file) > 0) {
            /* 已有文件内容原样保留；记录级解析由对应后续阶段实现。 */
        }
        if (ferror(file)) {
            report_file_error("读取文件", path);
            success = 0;
        }
    }
    if (!close_input(file, path)) {
        success = 0;
    }
    return success;
}

/* 功能：严格解析一条 编号|状态。参数：line 可修改行；computer 成功时输出。
 * 返回：有效 1，否则 0；尾随空白和 CRLF 可接受，多余字段不可接受。
 */
static int parse_computer(char *line, Lab *computer)
{
    char *separator = strchr(line, '|'); /* 只允许一个字段分隔符。 */
    int no; /* 校验后的编号。 */
    int status; /* 校验后的状态数值。 */
    if (separator == NULL || strchr(separator + 1, '|') != NULL) {
        return 0;
    }
    *separator = '\0';
    if (!utils_parse_int(line, 1, COMPUTER_COUNT, &no) ||
        !utils_parse_int(separator + 1, COMPUTER_NORMAL, COMPUTER_FAULT, &status)) {
        return 0;
    }
    computer->no = no;
    computer->status = (ComputerStatus)status;
    return 1;
}

/* 功能：独占新建临时快照，避免覆盖遗留文件。
 * 参数：temporary 临时路径。返回：成功为待关闭输出流，失败 NULL 并报告原因。
 */
static FILE *open_snapshot(const char *temporary)
{
    FILE *file; /* 返回给保存函数的临时输出流。 */
#ifdef _WIN32
    /* MinGW 可链接不支持 fopen("wbx") 的 MSVCRT，改用等价的 CRT 独占创建。 */
    int descriptor = _open(temporary,
                           _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY,
                           _S_IREAD | _S_IWRITE); /* 新文件的独占描述符。 */
    if (descriptor < 0) {
        report_file_error("独占创建临时文件（请检查是否有遗留 .tmp）", temporary);
        return NULL;
    }
    file = _fdopen(descriptor, "wb");
    if (file == NULL) {
        report_file_error("创建临时文件流", temporary);
        if (_close(descriptor) != 0) {
            report_file_error("关闭临时描述符", temporary);
        }
        return NULL;
    }
#else
    file = fopen(temporary, "wbx");
    if (file == NULL) {
        report_file_error("独占创建临时文件（请检查是否有遗留 .tmp）", temporary);
    }
#endif
    return file;
}

/* 功能：读取电脑完整快照，先在局部数组校验再发布；参数及返回值见 storage.h。 */
/* 功能、参数及返回值：见 storage.h 中 storage_load_computers 的契约。 */
int storage_load_computers(Lab computers[COMPUTER_COUNT])
{
    Lab loaded[COMPUTER_COUNT]; /* 临时快照，读取失败不污染调用方数据。 */
    int seen[COMPUTER_COUNT] = {0}; /* 检测重复编号。 */
    int count = 0; /* 已读有效记录条数。 */
    int result = -1; /* 默认失败；仅完整有效或真正零字节时改写。 */
    char line[INPUT_LINE_CAPACITY]; /* 固定行长度上限，超长行视为损坏。 */
    FILE *file; /* 电脑数据输入流。 */
    if (computers == NULL || check_path(COMPUTERS_PATH, 0) != 1) {
        fputs("无法加载电脑：缺少有效文件或输出数组。\n", stderr);
        return -1;
    }
    file = fopen(COMPUTERS_PATH, "rb");
    if (file == NULL) {
        report_file_error("打开电脑文件", COMPUTERS_PATH);
        return -1;
    }
    for (;;) {
        Lab record; /* 当前行解析结果。 */
        LineResult line_result = utils_read_line(file, line, sizeof line); /* 读行状态。 */
        if (line_result == LINE_EOF) {
            if (count == 0) {
                result = 0; /* 空白行会在下方报错，只有零字节文件到达此分支。 */
            } else if (count == COMPUTER_COUNT) {
                result = 1;
            } else {
                fprintf(stderr, "%s 记录不完整：需要 %d 条，实际 %d 条。\n",
                        COMPUTERS_PATH, COMPUTER_COUNT, count);
            }
            break;
        }
        if (line_result == LINE_ERROR) {
            report_file_error("读取电脑文件", COMPUTERS_PATH);
            break;
        }
        if (line_result != LINE_OK || count >= COMPUTER_COUNT ||
            !parse_computer(line, &record) || seen[record.no - 1]) {
            fprintf(stderr, "%s 第 %d 行损坏：编号、状态、长度或字段格式无效/重复。\n",
                    COMPUTERS_PATH, count + 1);
            break;
        }
        seen[record.no - 1] = 1;
        loaded[record.no - 1] = record; /* 文件可以乱序，内存统一按编号排列。 */
        ++count;
    }
    if (!close_input(file, COMPUTERS_PATH)) {
        return -1;
    }
    if (result == 1) {
        memcpy(computers, loaded, sizeof loaded);
    }
    return result;
}

/* 功能：同目录替换或归档一个明确文件；参数：源/目标路径。返回：成功 1，否则 0。 */
static int replace_path(const char *temporary, const char *target)
{
#ifdef _WIN32
    DWORD error_code = ERROR_SUCCESS;
    int attempt;
    /* Windows 索引/扫描可能短暂占用刚关闭的文件；只重试三类瞬时拒绝，最多等待100毫秒。
     * 持续锁仍返回失败，绝不先删除目标；本操作和单文件快照共用此规则。
     */
    for (attempt = 0; attempt < 5; ++attempt) {
        if (MoveFileExA(temporary, target, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) { return 1; }
        error_code = GetLastError();
        if (error_code != ERROR_SHARING_VIOLATION && error_code != ERROR_LOCK_VIOLATION &&
            error_code != ERROR_ACCESS_DENIED) { break; }
        if (attempt < 4) { Sleep((DWORD)(10 * (attempt + 1))); }
    }
    fprintf(stderr, "替换 '%s' 失败：Windows 错误 %lu；临时文件已保留。\n",
            target, (unsigned long)error_code);
    return 0;
#else
    if (rename(temporary, target) != 0) {
        report_file_error("替换文件（临时文件已保留）", target);
        return 0;
    }
#endif
    return 1;
}
/* 功能：统一刷新/关闭/替换快照，电脑和账户共用。
 * 参数：file 输出流（一定关闭）；target 目标；temporary 临时路径；success 之前写入结果。
 * 返回：全部成功 1，否则 0，旧目标不先删除，临时文件留供检查。
 */
static int finish_snapshot(FILE *file, const char *target, const char *temporary, int success)
{
    if (fflush(file) != 0) {
        report_file_error("刷新临时文件", temporary);
        success = 0;
    }
    if (fclose(file) != 0) {
        report_file_error("关闭临时文件", temporary);
        success = 0;
    }
    if (!success) {
        return 0; /* 保留旧目标与临时文件，便于检查失败原因。 */
    }
    return replace_path(temporary, target);
}

/* 功能：保存电脑快照，复用统一文件替换逻辑；参数与返回值见 storage.h。 */
static int save_computers_raw(const Lab computers[COMPUTER_COUNT])
{
    FILE *file; /* 独占新建的临时流。 */
    int i; /* 写入下标。 */
    int success = 1; /* 之前写入是否成功。 */
    if (!lab_validate_computers(computers)) {
        fputs("保存失败：电脑集合无效。\n", stderr);
        return 0;
    }
    file = open_snapshot(COMPUTERS_TEMP_PATH);
    if (file == NULL) {
        return 0;
    }
    for (i = 0; i < COMPUTER_COUNT; ++i) {
        if (fprintf(file, "%d|%d\n", computers[i].no, (int)computers[i].status) < 0) {
            report_file_error("写入临时文件", COMPUTERS_TEMP_PATH);
            success = 0;
            break;
        }
    }
    return finish_snapshot(file, COMPUTERS_PATH, COMPUTERS_TEMP_PATH, success);
}

/* 功能：按角色选择文件。参数：role 角色。返回：有效路径，非法角色返回 NULL。 */
static const char *account_path(UserRole role)
{
    return role == ROLE_ADMIN ? ADMINS_PATH : (role == ROLE_STUDENT ? STUDENTS_PATH : NULL);
}

/* 功能：取得指定账户标识。参数：records 集合；role 角色；index 有效下标。
 * 返回：只读标识地址，调用方必须先验证 role/index。
 */
static const char *record_id(const AccountRecords *records, UserRole role, size_t index)
{
    return role == ROLE_ADMIN ? records->admins[index].id : records->students[index].id;
}

/* 功能：严格拆分并校验一行账户。参数：line 可改缓冲区；role 角色；records 输出；index 下标。
 * 返回：有效 1，否则 0；调用方已检查 index 在角色容量内。
 */
static int parse_account(char *line, UserRole role, AccountRecords *records, size_t index)
{
    char *fields[4]; /* 工号/学号、姓名、密码，以及学生累计时间。 */
    size_t field_count = role == ROLE_ADMIN ? 3 : 4; /* 必须精确匹配字段数。 */
    size_t i; /* 字段下标。 */
    size_t length = strlen(line); /* read_line 已确保结尾与无嵌入 NUL。 */
    int elapsed = 0; /* 学生累计时间。 */
    if (length > 0 && line[length - 1] == '\r') {
        line[length - 1] = '\0';
    }
    fields[0] = line;
    for (i = 1; i < field_count; ++i) {
        char *separator = strchr(fields[i - 1], '|'); /* 当前字段的唯一边界。 */
        if (separator == NULL) {
            return 0;
        }
        *separator = '\0';
        fields[i] = separator + 1;
    }
    if (strchr(fields[field_count - 1], '|') != NULL ||
        !utils_valid_id(fields[0]) || !utils_valid_field(fields[1], NAME_LEN) ||
        !utils_valid_field(fields[2], PASSWORD_LEN)) {
        return 0;
    }
    if (role == ROLE_STUDENT) {
        for (i = 0; fields[3][i] != '\0'; ++i) {
            if (fields[3][i] < '0' || fields[3][i] > '9') {
                return 0;
            }
        }
        if (!utils_parse_int(fields[3], 0, INT_MAX, &elapsed)) {
            return 0;
        }
    }
    for (i = 0; i < index; ++i) {
        if (strcmp(record_id(records, role, i), fields[0]) == 0) {
            return 0;
        }
    }
    if (role == ROLE_ADMIN) {
        Admin *record = &records->admins[index]; /* 有界校验后才复制字段。 */
        strcpy(record->id, fields[0]);
        strcpy(record->name, fields[1]);
        strcpy(record->password, fields[2]);
    } else {
        Student *record = &records->students[index]; /* 学生输出位置。 */
        strcpy(record->id, fields[0]);
        strcpy(record->name, fields[1]);
        strcpy(record->password, fields[2]);
        record->time = elapsed;
    }
    return 1;
}

/* 功能：事务式发布读取结果，不将损坏文件当空表；参数与返回值见 storage.h。 */
/* 功能、参数及返回值：见 storage.h 中 storage_load_accounts 的契约。 */
int storage_load_accounts(UserRole role, AccountRecords *accounts, size_t *count)
{
    const char *path = account_path(role); /* 当前角色文件。 */
    AccountRecords *loaded; /* 临时集合，避免读一半修改调用方。 */
    FILE *file; /* 账户输入流。 */
    size_t used = 0; /* 已解析条数。 */
    size_t capacity = role == ROLE_ADMIN ? MAX_ADMINS : MAX_STUDENTS; /* 角色上限。 */
    int success = 1; /* 聚合读行与关闭结果。 */
    char line[ID_LEN + NAME_LEN + PASSWORD_LEN + 32]; /* 一条合法最长记录及整数。 */
    if (path == NULL || accounts == NULL || count == NULL) {
        fputs("账户读取参数无效。\n", stderr);
        return 0;
    }
    if (check_path(path, 0) != 1) {
        fprintf(stderr, "账户文件 '%s' 不可读取或缺失。\n", path);
        return 0;
    }
    file = fopen(path, "rb");
    if (file == NULL) {
        report_file_error("打开账户文件", path);
        return 0;
    }
    loaded = calloc(1, sizeof *loaded);
    if (loaded == NULL) {
        fputs("账户读取内存分配失败。\n", stderr);
        close_input(file, path);
        return 0;
    }
    for (;;) {
        LineResult result = utils_read_line(file, line, sizeof line); /* 当前行结果。 */
        if (result == LINE_EOF) {
            break;
        }
        if (result == LINE_ERROR) {
            report_file_error("读取账户文件", path);
            success = 0;
            break;
        }
        if (result != LINE_OK || used >= capacity || !parse_account(line, role, loaded, used)) {
            fprintf(stderr, "账户文件 '%s' 第 %zu 行损坏或超过容量；请检查格式/重复标识。\n",
                    path, used + 1);
            success = 0;
            break;
        }
        ++used;
    }
    utils_clear_secret(line, sizeof line);
    if (!close_input(file, path)) {
        success = 0;
    }
    if (success) {
        memcpy(accounts, loaded, sizeof *loaded);
        *count = used;
    }
    utils_clear_secret(loaded, sizeof *loaded);
    free(loaded);
    return success;
}

/* 功能：校验并保存账户集合，不在认证模块直接访问文件；契约见 storage.h。 */
/* 功能、参数及返回值：见 storage.h 中 storage_save_accounts 的契约。 */
int storage_save_accounts(UserRole role, const AccountRecords *accounts, size_t count)
{
    const char *path = account_path(role); /* 目标路径。 */
    const char *temporary = role == ROLE_ADMIN ? ADMINS_TEMP_PATH : STUDENTS_TEMP_PATH; /* 临时路径。 */
    size_t capacity = role == ROLE_ADMIN ? MAX_ADMINS : MAX_STUDENTS; /* 条数上限。 */
    size_t i; /* 当前账户。 */
    size_t j; /* 查重下标。 */
    FILE *file; /* 校验完成后才独占创建的输出流。 */
    int success = 1; /* 写入状态。 */
    if (!storage_writes_allowed()) { return 0; }
    if (path == NULL || accounts == NULL || count > capacity) {
        fputs("账户保存参数无效。\n", stderr);
        return 0;
    }
    for (i = 0; i < count; ++i) {
        const char *id = record_id(accounts, role, i); /* 只读取当前角色成员。 */
        const char *name = role == ROLE_ADMIN ? accounts->admins[i].name : accounts->students[i].name;
        const char *password = role == ROLE_ADMIN ? accounts->admins[i].password : accounts->students[i].password;
        if (!utils_valid_id(id) || !utils_valid_field(name, NAME_LEN) ||
            !utils_valid_field(password, PASSWORD_LEN) ||
            (role == ROLE_STUDENT && accounts->students[i].time < 0)) {
            fputs("账户快照字段无效，未保存。\n", stderr);
            return 0;
        }
        for (j = 0; j < i; ++j) {
            if (strcmp(record_id(accounts, role, j), id) == 0) {
                fputs("账户快照标识重复，未保存。\n", stderr);
                return 0;
            }
        }
    }
    file = open_snapshot(temporary);
    if (file == NULL) {
        return 0;
    }
    for (i = 0; i < count; ++i) {
        int written; /* fprintf 的结果，不输出原记录或密码到诊断。 */
        if (role == ROLE_ADMIN) {
            const Admin *a = &accounts->admins[i]; /* 管理员记录。 */
            written = fprintf(file, "%s|%s|%s\n", a->id, a->name, a->password);
        } else {
            const Student *s = &accounts->students[i]; /* 学生记录。 */
            written = fprintf(file, "%s|%s|%s|%d\n", s->id, s->name, s->password, s->time);
        }
        if (written < 0) {
            report_file_error("写入账户临时文件", temporary);
            success = 0;
            break;
        }
    }
    return finish_snapshot(file, path, temporary, success);
}

/* 功能：启动时准备文件并加载数据，仅在电脑缺失或零字节时初始化；契约见 storage.h。 */
/* 功能、参数及返回值：见 storage.h 中 storage_initialize 的契约。 */
int storage_initialize(Lab computers[COMPUTER_COUNT])
{
    static const char *const paths[] = {
        ADMINS_PATH, STUDENTS_PATH, RESERVATIONS_PATH, REPORTS_PATH, COUNTS_PATH
    }; /* 引导创建与可读性检查；账户和预约由各加载器按需严格解析。 */
    size_t i; /* 固定文件路径表下标。 */
    int exists; /* 电脑文件是否存在且类型正确。 */
    int loaded; /* 电脑文件读取结果。 */
    Lab defaults[COMPUTER_COUNT]; /* 保存成功后才交给调用方的默认数据。 */
    if (computers == NULL || !ensure_data_directory()) {
        return 0;
    }
    if (storage_recover_maintenance() != STORAGE_OK) { return 0; }
    for (i = 0; i < sizeof paths / sizeof paths[0]; ++i) {
        if (!ensure_readable_file(paths[i])) {
            return 0;
        }
    }
    exists = check_path(COMPUTERS_PATH, 0);
    if (exists < 0) {
        return 0;
    }
    loaded = exists ? storage_load_computers(computers) : 0;
    if (loaded < 0) {
        return 0; /* 损坏不能退回默认数据，也不能覆盖。 */
    }
    if (loaded == 1) {
        return 1;
    }
    lab_init_computers(defaults);
    if (!storage_save_computers(defaults)) {
        return 0;
    }
    memcpy(computers, defaults, sizeof defaults);
    return 1;
}

/* 功能：验证集合内存形状。参数：list。返回：有效 1，否则 0，不访问数组内容。 */
static int reservation_shape(const ReservationList *list)
{
    return list != NULL && list->count <= list->capacity && list->capacity <= MAX_RESERVATIONS &&
        ((list->capacity == 0 && list->items == NULL) || (list->capacity > 0 && list->items != NULL));
}

/* 功能：有界动态扩容；参数、返回值见 storage.h。 */
/* 功能、参数及返回值：见 storage.h 中 storage_reserve_reservations 的契约。 */
StorageResult storage_reserve_reservations(ReservationList *list, size_t needed)
{
    size_t capacity; /* 候选容量，失败时不发布。 */
    Reservation *grown; /* realloc 临时返回值，不能直接覆盖原指针。 */
    if (needed > MAX_RESERVATIONS) { return STORAGE_CAPACITY; }
    if (!reservation_shape(list)) { return STORAGE_ERROR; }
    if (needed <= list->capacity) { return STORAGE_OK; }
    capacity = list->capacity == 0 ? 64 : list->capacity;
    while (capacity < needed) {
        capacity = capacity > MAX_RESERVATIONS / 2 ? MAX_RESERVATIONS : capacity * 2;
    }
    if (capacity > SIZE_MAX / sizeof *grown) { return STORAGE_CAPACITY; }
    grown = realloc(list->items, capacity * sizeof *grown);
    if (grown == NULL) { return STORAGE_NO_MEMORY; }
    list->items = grown;
    list->capacity = capacity;
    return STORAGE_OK;
}

/* 功能：释放并清空预约集合；参数、返回值见 storage.h。 */
/* 功能、参数及返回值：见 storage.h 中 storage_free_reservations 的契约。 */
void storage_free_reservations(ReservationList *list)
{
    if (list != NULL) {
        free(list->items);
        memset(list, 0, sizeof *list);
    }
}

/* 功能：按正整数编号比较索引。参数：left/right 指向记录指针。返回：排序顺序。 */
static int reservation_id_order(const void *left, const void *right)
{
    const Reservation *a = *(const Reservation *const *)left; /* 左记录。 */
    const Reservation *b = *(const Reservation *const *)right; /* 右记录。 */
    return (a->id > b->id) - (a->id < b->id); /* 禁止减法溢出。 */
}

/* 功能：按日期、时段、电脑比较索引。参数：left/right 指向记录指针。返回：排序顺序。 */
static int reservation_seat_order(const void *left, const void *right)
{
    const Reservation *a = *(const Reservation *const *)left; /* 左记录。 */
    const Reservation *b = *(const Reservation *const *)right; /* 右记录。 */
    if (a->date != b->date) { return (a->date > b->date) - (a->date < b->date); }
    if (a->slot_index != b->slot_index) { return (a->slot_index > b->slot_index) - (a->slot_index < b->slot_index); }
    return (a->computer_no > b->computer_no) - (a->computer_no < b->computer_no);
}

/* 功能：按学生、日期、时段比较索引。参数：left/right 指向记录指针。返回：排序顺序。 */
static int reservation_student_order(const void *left, const void *right)
{
    const Reservation *a = *(const Reservation *const *)left; /* 左记录。 */
    const Reservation *b = *(const Reservation *const *)right; /* 右记录。 */
    int order = strcmp(a->student_id, b->student_id); /* 字段已先验证终止与格式。 */
    if (order != 0) { return order; }
    if (a->date != b->date) { return (a->date > b->date) - (a->date < b->date); }
    return (a->slot_index > b->slot_index) - (a->slot_index < b->slot_index);
}

/* 功能：完整快照校验，只排序指针索引，不改原记录顺序；契约见 storage.h。 */
/* 功能、参数及返回值：见 storage.h 中 storage_validate_reservations 的契约。 */
StorageResult storage_validate_reservations(const ReservationList *list)
{
    const Reservation **index; /* 堆上索引，避免 n² 查重与大栈数组。 */
    size_t i; /* 当前记录。 */
    size_t active = 0; /* 有效记录数，已取消不参与冲突。 */
    StorageResult result = STORAGE_ERROR; /* 校验完所有约束才成功。 */
    if (list != NULL && list->count > MAX_RESERVATIONS) { return STORAGE_CAPACITY; }
    if (!reservation_shape(list)) { return STORAGE_ERROR; }
    if (list->count == 0) { return STORAGE_OK; }
    for (i = 0; i < list->count; ++i) {
        const Reservation *r = &list->items[i]; /* 先检查字段再用于比较器。 */
        if (r->id <= 0 || !utils_valid_id(r->student_id) || r->computer_no < 1 ||
            r->computer_no > COMPUTER_COUNT || !utils_valid_date(r->date) ||
            r->slot_index < 0 || r->slot_index >= SLOT_COUNT ||
            (r->status != BOOKING_ACTIVE && r->status != BOOKING_CANCELLED)) {
            return STORAGE_ERROR;
        }
    }
    if (list->count > SIZE_MAX / sizeof *index) { return STORAGE_CAPACITY; }
    index = malloc(list->count * sizeof *index);
    if (index == NULL) { return STORAGE_NO_MEMORY; }
    for (i = 0; i < list->count; ++i) { index[i] = &list->items[i]; }
    qsort(index, list->count, sizeof *index, reservation_id_order);
    for (i = 1; i < list->count; ++i) {
        if (index[i - 1]->id == index[i]->id) { goto done; }
    }
    for (i = 0; i < list->count; ++i) {
        if (list->items[i].status == BOOKING_ACTIVE) { index[active++] = &list->items[i]; }
    }
    qsort(index, active, sizeof *index, reservation_seat_order);
    for (i = 1; i < active; ++i) {
        if (reservation_seat_order(&index[i - 1], &index[i]) == 0) { goto done; }
    }
    qsort(index, active, sizeof *index, reservation_student_order);
    for (i = 1; i < active; ++i) {
        if (reservation_student_order(&index[i - 1], &index[i]) == 0) { goto done; }
    }
    result = STORAGE_OK;
done:
    free(index);
    return result;
}

/* 功能：严格数字字段，不允许符号/空白。参数：text/minimum/maximum；value 输出。返回：有效 1，否则 0。 */
static int reservation_number(const char *text, int minimum, int maximum, int *value)
{
    size_t i; /* 字节位置。 */
    for (i = 0; text[i] != '\0'; ++i) {
        if (text[i] < '0' || text[i] > '9') { return 0; }
    }
    return utils_parse_int(text, minimum, maximum, value);
}

/* 功能：解析恰好六字段预约。参数：line 可改行；record 输出。返回：有效 1，否则 0。 */
static int parse_reservation(char *line, Reservation *record)
{
    char *fields[6]; /* 编号、学号、电脑、日期、时段、状态。 */
    size_t i; /* 字段下标。 */
    size_t length = strlen(line); /* 已经拒绝 NUL 的行。 */
    int status; /* 经过范围检查的状态。 */
    if (length > 0 && line[length - 1] == '\r') { line[length - 1] = '\0'; }
    fields[0] = line;
    for (i = 1; i < 6; ++i) {
        char *separator = strchr(fields[i - 1], '|'); /* 下一个分隔符。 */
        if (separator == NULL) { return 0; }
        *separator = '\0'; fields[i] = separator + 1;
    }
    if (strchr(fields[5], '|') != NULL || !utils_valid_id(fields[1]) ||
        !reservation_number(fields[0], 1, INT_MAX, &record->id) ||
        !reservation_number(fields[2], 1, COMPUTER_COUNT, &record->computer_no) ||
        strlen(fields[3]) != 8 || !reservation_number(fields[3], 10000101, 99991231, &record->date) ||
        !utils_valid_date(record->date) ||
        !reservation_number(fields[4], 0, SLOT_COUNT - 1, &record->slot_index) ||
        !reservation_number(fields[5], BOOKING_CANCELLED, BOOKING_ACTIVE, &status)) {
        return 0;
    }
    strcpy(record->student_id, fields[1]);
    record->status = (BookingStatus)status;
    return 1;
}

/* 功能：严格加载，部分失败不发布，也不覆盖源文件；契约见 storage.h。 */
/* 功能、参数及返回值：见 storage.h 中 storage_load_reservations 的契约。 */
StorageResult storage_load_reservations(ReservationList *list)
{
    ReservationList loaded = {0}; /* 本次读取的独立快照。 */
    StorageResult result = STORAGE_OK; /* 汇总解析、分配、全量验证与关闭。 */
    FILE *file; /* 预约输入流。 */
    char line[INPUT_LINE_CAPACITY]; /* 有界记录行。 */
    if (!reservation_shape(list) || check_path(RESERVATIONS_PATH, 0) != 1) {
        fputs("预约文件缺失、类型错误或输出集合无效。\n", stderr);
        return STORAGE_ERROR;
    }
    file = fopen(RESERVATIONS_PATH, "rb");
    if (file == NULL) { report_file_error("打开预约文件", RESERVATIONS_PATH); return STORAGE_ERROR; }
    for (;;) {
        Reservation record = {0}; /* 当前记录。 */
        LineResult line_result = utils_read_line(file, line, sizeof line); /* 读行结果。 */
        if (line_result == LINE_EOF) { break; }
        if (line_result == LINE_ERROR) {
            report_file_error("读取预约文件", RESERVATIONS_PATH); result = STORAGE_ERROR; break;
        }
        if (line_result != LINE_OK || !parse_reservation(line, &record)) {
            fprintf(stderr, "预约文件 '%s' 第 %zu 行损坏。\n", RESERVATIONS_PATH, loaded.count + 1);
            result = STORAGE_ERROR; break;
        }
        result = storage_reserve_reservations(&loaded, loaded.count + 1);
        if (result != STORAGE_OK) { break; }
        loaded.items[loaded.count++] = record;
    }
    if (!close_input(file, RESERVATIONS_PATH)) { result = STORAGE_ERROR; }
    if (result == STORAGE_OK) { result = storage_validate_reservations(&loaded); }
    if (result == STORAGE_OK) {
        storage_free_reservations(list);
        *list = loaded; /* 完整成功后转交数组所有权。 */
    } else {
        fputs("预约加载失败：请检查文件完整性、容量或内存；原文件未改动。\n", stderr);
        storage_free_reservations(&loaded);
    }
    return result;
}

/* 功能：验证并安全保存预约；契约见 storage.h。 */
static StorageResult save_reservations_raw(const ReservationList *list)
{
    StorageResult result = storage_validate_reservations(list); /* 保存前重新全量校验。 */
    FILE *file; /* 独占临时流。 */
    size_t i; /* 写入下标。 */
    int success = 1; /* 每一条及最终关闭均需成功。 */
    if (result != STORAGE_OK) {
        fputs("预约快照校验失败，未保存。\n", stderr); return result;
    }
    file = open_snapshot(RESERVATIONS_TEMP_PATH);
    if (file == NULL) { return STORAGE_ERROR; }
    for (i = 0; i < list->count; ++i) {
        const Reservation *r = &list->items[i]; /* 已验证记录。 */
        if (fprintf(file, "%d|%s|%d|%d|%d|%d\n", r->id, r->student_id, r->computer_no,
                    r->date, r->slot_index, (int)r->status) < 0) {
            report_file_error("写入预约临时文件", RESERVATIONS_TEMP_PATH); success = 0; break;
        }
    }
    return finish_snapshot(file, RESERVATIONS_PATH, RESERVATIONS_TEMP_PATH, success) ? STORAGE_OK : STORAGE_ERROR;
}

/* 功能：验证报告集合内存形状；参数：list。返回：有效 1，否则 0。 */
static int report_shape(const FaultReportList *list)
{
    return list != NULL && list->count <= list->capacity && list->capacity <= MAX_REPORTS &&
        ((list->capacity == 0 && list->items == NULL) || (list->capacity > 0 && list->items != NULL));
}

/* 功能、参数及返回值：见 storage.h 中 storage_reserve_reports 的契约。 */
StorageResult storage_reserve_reports(FaultReportList *list, size_t needed)
{
    size_t capacity;
    FaultReport *grown;
    if (needed > MAX_REPORTS) { return STORAGE_CAPACITY; }
    if (!report_shape(list)) { return STORAGE_ERROR; }
    if (needed <= list->capacity) { return STORAGE_OK; }
    capacity = list->capacity == 0 ? 32 : list->capacity;
    while (capacity < needed) { capacity = capacity > MAX_REPORTS / 2 ? MAX_REPORTS : capacity * 2; }
    if (capacity > SIZE_MAX / sizeof *grown) { return STORAGE_CAPACITY; }
    grown = realloc(list->items, capacity * sizeof *grown);
    if (grown == NULL) { return STORAGE_NO_MEMORY; }
    list->items = grown; list->capacity = capacity;
    return STORAGE_OK;
}

/* 功能、参数及返回值：见 storage.h 中 storage_free_reports 的契约。 */
void storage_free_reports(FaultReportList *list)
{
    if (list != NULL) { free(list->items); memset(list, 0, sizeof *list); }
}

/* 功能：报告指针按编号比较；参数：a/b 索引项。返回：顺序。 */
static int report_id_order(const void *a, const void *b)
{
    const FaultReport *x = *(const FaultReport *const *)a;
    const FaultReport *y = *(const FaultReport *const *)b;
    return (x->id > y->id) - (x->id < y->id);
}

/* 功能：待处理报告按学生及电脑比较；参数：a/b 索引项。返回：顺序。 */
static int report_key_order(const void *a, const void *b)
{
    const FaultReport *x = *(const FaultReport *const *)a;
    const FaultReport *y = *(const FaultReport *const *)b;
    int order = strcmp(x->student_id, y->student_id);
    return order != 0 ? order : (x->computer_no > y->computer_no) - (x->computer_no < y->computer_no);
}

/* 功能、参数及返回值：见 storage.h 中 storage_validate_reports 的契约。 */
StorageResult storage_validate_reports(const FaultReportList *list)
{
    const FaultReport **index;
    size_t i, pending = 0;
    StorageResult result = STORAGE_ERROR;
    if (list != NULL && list->count > MAX_REPORTS) { return STORAGE_CAPACITY; }
    if (!report_shape(list)) { return STORAGE_ERROR; }
    if (list->count == 0) { return STORAGE_OK; }
    for (i = 0; i < list->count; ++i) {
        const FaultReport *r = &list->items[i];
        if (r->id <= 0 || !utils_valid_id(r->student_id) || r->computer_no < 1 ||
            r->computer_no > COMPUTER_COUNT || !utils_valid_date(r->date) ||
            !utils_valid_field(r->detail, DETAIL_LEN) ||
            (r->status != REPORT_PENDING && r->status != REPORT_PROCESSED)) { return STORAGE_ERROR; }
    }
    index = malloc(list->count * sizeof *index);
    if (index == NULL) { return STORAGE_NO_MEMORY; }
    for (i = 0; i < list->count; ++i) { index[i] = &list->items[i]; }
    qsort(index, list->count, sizeof *index, report_id_order);
    for (i = 1; i < list->count; ++i) { if (index[i-1]->id == index[i]->id) { goto done; } }
    for (i = 0; i < list->count; ++i) {
        if (list->items[i].status == REPORT_PENDING) { index[pending++] = &list->items[i]; }
    }
    qsort(index, pending, sizeof *index, report_key_order);
    for (i = 1; i < pending; ++i) { if (report_key_order(&index[i-1], &index[i]) == 0) { goto done; } }
    result = STORAGE_OK;
done:
    free(index); return result;
}

/* 功能：严格拆分固定字段数，兼容 CRLF。参数：line 可改；fields 输出；count 字段数。返回：有效 1。 */
static int split_fields(char *line, char **fields, size_t count)
{
    size_t i, length = strlen(line);
    if (length > 0 && line[length-1] == '\r') { line[length-1] = '\0'; }
    fields[0] = line;
    for (i = 1; i < count; ++i) {
        char *next = strchr(fields[i-1], '|');
        if (next == NULL) { return 0; }
        *next = '\0'; fields[i] = next + 1;
    }
    return strchr(fields[count-1], '|') == NULL;
}

/* 功能：解析六字段报告。参数：line 可改；r 输出。返回：有效 1，否则 0。 */
static int parse_report(char *line, FaultReport *r)
{
    char *f[6];
    int status;
    if (!split_fields(line, f, 6) || !reservation_number(f[0], 1, INT_MAX, &r->id) ||
        !utils_valid_id(f[1]) || !reservation_number(f[2], 1, COMPUTER_COUNT, &r->computer_no) ||
        strlen(f[3]) != 8 || !reservation_number(f[3], 10000101, 99991231, &r->date) ||
        !utils_valid_date(r->date) || !utils_valid_field(f[4], DETAIL_LEN) ||
        !reservation_number(f[5], REPORT_PENDING, REPORT_PROCESSED, &status)) { return 0; }
    strcpy(r->student_id, f[1]); strcpy(r->detail, f[4]); r->status = (ReportStatus)status;
    return 1;
}

/* 功能、参数及返回值：见 storage.h 中 storage_load_reports 的契约。 */
StorageResult storage_load_reports(FaultReportList *list)
{
    FaultReportList loaded = {0};
    StorageResult result = STORAGE_OK;
    char line[ID_LEN + DETAIL_LEN + 64];
    FILE *file;
    if (!report_shape(list) || check_path(REPORTS_PATH, 0) != 1) { return STORAGE_ERROR; }
    file = fopen(REPORTS_PATH, "rb");
    if (file == NULL) { report_file_error("打开报告", REPORTS_PATH); return STORAGE_ERROR; }
    for (;;) {
        FaultReport r = {0};
        LineResult read = utils_read_line(file, line, sizeof line);
        if (read == LINE_EOF) { break; }
        if (read != LINE_OK || !parse_report(line, &r)) {
            fprintf(stderr, "报告文件 '%s' 第 %zu 行损坏或读取失败。\n", REPORTS_PATH, loaded.count+1);
            result = STORAGE_ERROR; break;
        }
        result = storage_reserve_reports(&loaded, loaded.count+1);
        if (result != STORAGE_OK) { break; }
        loaded.items[loaded.count++] = r;
    }
    if (!close_input(file, REPORTS_PATH)) { result = STORAGE_ERROR; }
    if (result == STORAGE_OK) { result = storage_validate_reports(&loaded); }
    if (result == STORAGE_OK) { storage_free_reports(list); *list = loaded; }
    else { fputs("报告加载失败，未改动原文件。\n", stderr); storage_free_reports(&loaded); }
    return result;
}

/* 功能：恢复专用的报告快照保存，不通过待恢复门禁；参数：完整集合。返回：StorageResult。 */
static StorageResult save_reports_raw(const FaultReportList *list)
{
    StorageResult result = storage_validate_reports(list);
    FILE *file;
    size_t i;
    int success = 1;
    if (result != STORAGE_OK) { return result; }
    file = open_snapshot(REPORTS_TEMP_PATH);
    if (file == NULL) { return STORAGE_ERROR; }
    for (i = 0; i < list->count; ++i) {
        const FaultReport *r = &list->items[i];
        if (fprintf(file, "%d|%s|%d|%08d|%s|%d\n", r->id, r->student_id, r->computer_no,
                    r->date, r->detail, (int)r->status) < 0) { success = 0; break; }
    }
    return finish_snapshot(file, REPORTS_PATH, REPORTS_TEMP_PATH, success) ? STORAGE_OK : STORAGE_ERROR;
}

/* 功能、参数及返回值：见 storage.h 中 storage_writes_allowed 的契约。 */
int storage_writes_allowed(void)
{
    if (check_path(MAINTENANCE_PATH, 0) == 0) { return 1; }
    fputs("维护尚未完成或日志不可访问：请重启恢复；已阻止相关业务写入/查询。\n", stderr);
    return 0;
}

/* 功能、参数及返回值：见 storage.h 中 storage_save_computers 的契约。 */
int storage_save_computers(const Lab computers[COMPUTER_COUNT])
{
    return storage_writes_allowed() && save_computers_raw(computers);
}

/* 功能、参数及返回值：见 storage.h 中 storage_save_reservations 的契约。 */
StorageResult storage_save_reservations(const ReservationList *list)
{
    return storage_writes_allowed() ? save_reservations_raw(list) : STORAGE_RECOVERY_REQUIRED;
}

/* 功能、参数及返回值：见 storage.h 中 storage_save_reports 的契约。 */
StorageResult storage_save_reports(const FaultReportList *list)
{
    return storage_writes_allowed() ? save_reports_raw(list) : STORAGE_RECOVERY_REQUIRED;
}

/* 单条日志对应一个确定性操作；已确认时间在恢复中固定，不采用重启时刻。 */
typedef struct {
    MaintenanceAction action;
    int no;
    int report_id;
    LabTime cutoff;
} MaintenanceIntent;

/* 功能：验证日志字段组合。参数：intent。返回：有效 1。 */
static int valid_intent(const MaintenanceIntent *intent)
{
    return intent != NULL && intent->action >= STORAGE_MARK_FAULT && intent->action <= STORAGE_CLOSE_REPORT &&
        intent->no >= 1 && intent->no <= COMPUTER_COUNT && intent->report_id >= 0 &&
        (intent->action != STORAGE_CLOSE_REPORT || intent->report_id > 0) && utils_time_valid(&intent->cutoff);
}

/* 功能：预读完整相关快照、校验关联报告；参数：intent/输出集合/replay 是否恢复。返回：StorageResult。 */
static StorageResult load_maintenance(const MaintenanceIntent *intent, Lab computers[COMPUTER_COUNT],
                                    ReservationList *bookings, FaultReportList *reports, int replay)
{
    size_t i;
    StorageResult result;
    if (!valid_intent(intent)) { return STORAGE_ERROR; }
    if (intent->action != STORAGE_CLOSE_REPORT) {
        if (storage_load_computers(computers) != 1) { return STORAGE_ERROR; }
        result = storage_load_reservations(bookings);
        if (result != STORAGE_OK) { return result; }
    }
    if (intent->report_id == 0) { return STORAGE_OK; }
    result = storage_load_reports(reports);
    if (result != STORAGE_OK) { return result; }
    for (i = 0; i < reports->count; ++i) {
        const FaultReport *r = &reports->items[i];
        if (r->id == intent->report_id) {
            return r->computer_no == intent->no && (replay || r->status == REPORT_PENDING) ? STORAGE_OK : STORAGE_ERROR;
        }
    }
    return STORAGE_ERROR;
}

/* 功能：逐个归档本次已发布意图产生的残留快照，绝不批量删除。
 * 参数：path 唯一临时文件；archive 明确备份路径。返回：可继续 1，否则 0。
 */
static int archive_draft(const char *path, const char *archive)
{
    int exists = check_path(path, 0);
    return exists == 0 || (exists == 1 && replace_path(path, archive));
}

/* 功能：按电脑、预约、报告顺序重放，再归档意图。参数：intent；replay 恢复模式。返回：StorageResult。 */
static StorageResult execute_maintenance(const MaintenanceIntent *intent, int replay)
{
    Lab computers[COMPUTER_COUNT];
    ReservationList bookings = {0};
    FaultReportList reports = {0};
    StorageResult result = load_maintenance(intent, computers, &bookings, &reports, replay);
    size_t i;
    int computer_changed = 0, bookings_changed = 0, report_changed = 0;
    if (result != STORAGE_OK) { goto done; }
    if (replay &&
        ((intent->action != STORAGE_CLOSE_REPORT &&
          (!archive_draft(COMPUTERS_TEMP_PATH, DATA_DIRECTORY "/computers.recovery.txt") ||
           !archive_draft(RESERVATIONS_TEMP_PATH, DATA_DIRECTORY "/reservations.recovery.txt"))) ||
         (intent->report_id > 0 && !archive_draft(REPORTS_TEMP_PATH, DATA_DIRECTORY "/error.recovery.txt")))) {
        result = STORAGE_ERROR; goto done;
    }
    if (intent->action != STORAGE_CLOSE_REPORT) {
        ComputerStatus target = intent->action == STORAGE_MARK_FAULT ? COMPUTER_FAULT : COMPUTER_NORMAL;
        computer_changed = computers[intent->no-1].status != target;
        if (!lab_set_status(computers, intent->no, target)) { result = STORAGE_ERROR; goto done; }
    }
    if (intent->action == STORAGE_MARK_FAULT) {
        for (i = 0; i < bookings.count; ++i) {
            Reservation *r = &bookings.items[i];
            if (r->computer_no == intent->no && r->status == BOOKING_ACTIVE &&
                utils_slot_future(&intent->cutoff, r->date, r->slot_index)) {
                r->status = BOOKING_CANCELLED; bookings_changed = 1;
            }
        }
    }
    for (i = 0; i < reports.count; ++i) {
        if (reports.items[i].id == intent->report_id && reports.items[i].status == REPORT_PENDING) {
            reports.items[i].status = REPORT_PROCESSED; report_changed = 1;
        }
    }
    if ((computer_changed && !save_computers_raw(computers)) ||
        (bookings_changed && save_reservations_raw(&bookings) != STORAGE_OK) ||
        (report_changed && save_reports_raw(&reports) != STORAGE_OK) ||
        !replace_path(MAINTENANCE_PATH, MAINTENANCE_DONE_PATH)) { result = STORAGE_ERROR; }
done:
    storage_free_reservations(&bookings); storage_free_reports(&reports);
    if (result != STORAGE_OK) {
        fputs("维护未完成：保留意图日志，需重启恢复，禁止报告操作成功。\n", stderr);
        return STORAGE_RECOVERY_REQUIRED;
    }
    return STORAGE_OK;
}

/* 功能、参数及返回值：见 storage.h 中 storage_apply_maintenance 的契约。 */
StorageResult storage_apply_maintenance(MaintenanceAction action, int no, int report_id, const LabTime *cutoff)
{
    MaintenanceIntent intent;
    Lab computers[COMPUTER_COUNT];
    ReservationList bookings = {0};
    FaultReportList reports = {0};
    StorageResult result;
    FILE *file;
    int written;
    if (cutoff == NULL) { return STORAGE_ERROR; }
    intent.action = action; intent.no = no; intent.report_id = report_id; intent.cutoff = *cutoff;
    if (!storage_writes_allowed()) { return STORAGE_RECOVERY_REQUIRED; }
    result = load_maintenance(&intent, computers, &bookings, &reports, 0);
    storage_free_reservations(&bookings); storage_free_reports(&reports);
    if (result != STORAGE_OK) { return result; }
    /* 发布意图前不得有不属于本操作的残留快照；不覆盖、不删除。 */
    if ((action != STORAGE_CLOSE_REPORT &&
         (check_path(COMPUTERS_TEMP_PATH, 0) != 0 || check_path(RESERVATIONS_TEMP_PATH, 0) != 0)) ||
        (report_id > 0 && check_path(REPORTS_TEMP_PATH, 0) != 0)) { return STORAGE_ERROR; }
    file = open_snapshot(MAINTENANCE_TEMP_PATH);
    if (file == NULL) { return STORAGE_ERROR; }
    written = fprintf(file, "P3|1|%d|%d|%d|%08d|%d|%d|%d\n", (int)action, no, report_id,
                      cutoff->date, cutoff->hour, cutoff->minute, cutoff->second);
    if (!finish_snapshot(file, MAINTENANCE_PATH, MAINTENANCE_TEMP_PATH, written >= 0)) { return STORAGE_ERROR; }
    return execute_maintenance(&intent, 0);
}

/* 功能、参数及返回值：见 storage.h 中 storage_recover_maintenance 的契约。 */
StorageResult storage_recover_maintenance(void)
{
    MaintenanceIntent intent = {0};
    FILE *file;
    char line[160], extra[4], *f[9];
    int action, valid, exists = check_path(MAINTENANCE_PATH, 0);
    if (exists == 0) { return STORAGE_OK; }
    if (exists != 1) { return STORAGE_RECOVERY_REQUIRED; }
    file = fopen(MAINTENANCE_PATH, "rb");
    if (file == NULL) { return STORAGE_RECOVERY_REQUIRED; }
    valid = utils_read_line(file, line, sizeof line) == LINE_OK && split_fields(line, f, 9) &&
        strcmp(f[0], "P3") == 0 && strcmp(f[1], "1") == 0 &&
        reservation_number(f[2], STORAGE_MARK_FAULT, STORAGE_CLOSE_REPORT, &action) &&
        reservation_number(f[3], 1, COMPUTER_COUNT, &intent.no) &&
        reservation_number(f[4], 0, INT_MAX, &intent.report_id) && strlen(f[5]) == 8 &&
        reservation_number(f[5], 10000101, 99991231, &intent.cutoff.date) &&
        reservation_number(f[6], 0, 23, &intent.cutoff.hour) &&
        reservation_number(f[7], 0, 59, &intent.cutoff.minute) &&
        reservation_number(f[8], 0, 59, &intent.cutoff.second) &&
        utils_read_line(file, extra, sizeof extra) == LINE_EOF;
    if (!close_input(file, MAINTENANCE_PATH)) { valid = 0; }
    if (!valid) { fputs("维护日志损坏，阻止启动与相关写入。\n", stderr); return STORAGE_RECOVERY_REQUIRED; }
    intent.action = (MaintenanceAction)action;
    if (!valid_intent(&intent)) { return STORAGE_RECOVERY_REQUIRED; }
    return execute_maintenance(&intent, 1);
}
