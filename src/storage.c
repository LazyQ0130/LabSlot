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
#ifdef _WIN32
    /* Windows 的标准 rename 不能覆盖已有目标；同目录移动不先删除旧文件。 */
    if (!MoveFileExA(temporary, target,
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        fprintf(stderr, "替换 '%s' 失败：Windows 错误 %lu；临时文件已保留。\n",
                target, (unsigned long)GetLastError());
        return 0;
    }
#else
    if (rename(temporary, target) != 0) {
        report_file_error("替换文件（临时文件已保留）", target);
        return 0;
    }
#endif
    return 1;
}

/* 功能：保存电脑快照，复用统一文件替换逻辑；参数与返回值见 storage.h。 */
int storage_save_computers(const Lab computers[COMPUTER_COUNT])
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
int storage_save_accounts(UserRole role, const AccountRecords *accounts, size_t count)
{
    const char *path = account_path(role); /* 目标路径。 */
    const char *temporary = role == ROLE_ADMIN ? ADMINS_TEMP_PATH : STUDENTS_TEMP_PATH; /* 临时路径。 */
    size_t capacity = role == ROLE_ADMIN ? MAX_ADMINS : MAX_STUDENTS; /* 条数上限。 */
    size_t i; /* 当前账户。 */
    size_t j; /* 查重下标。 */
    FILE *file; /* 校验完成后才独占创建的输出流。 */
    int success = 1; /* 写入状态。 */
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
StorageResult storage_save_reservations(const ReservationList *list)
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
