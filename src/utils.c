#include "utils.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "model.h"

#ifdef _WIN32
#include <windows.h>
#endif

/* 功能：消费完整物理行并检测过长/NUL；参数及返回值见 utils.h。 */
LineResult utils_read_line(FILE *stream, char *buffer, size_t capacity)
{
    size_t used = 0; /* 已存入的字节数，不含结尾 NUL。 */
    int ch; /* fgetc 返回 int，以便正确区分所有字节与 EOF。 */
    int too_long = 0; /* 缓冲区满后仍消费余下输入。 */
    int invalid = 0; /* 嵌入 NUL 不应使后续解析提前结束。 */
    int any = 0; /* 是否已读取最后一行的任意字节。 */

    if (stream == NULL || buffer == NULL || capacity < 2) {
        return LINE_ERROR;
    }
    while ((ch = fgetc(stream)) != EOF && ch != '\n') {
        any = 1;
        if (ch == '\0') {
            invalid = 1;
        }
        if (used < capacity - 1) {
            buffer[used++] = (char)ch;
        } else {
            too_long = 1;
        }
    }
    buffer[used] = '\0';
    if (ferror(stream)) {
        return LINE_ERROR;
    }
    if (ch == EOF && !any) {
        return LINE_EOF;
    }
    if (too_long) {
        return LINE_TOO_LONG;
    }
    return invalid ? LINE_INVALID : LINE_OK;
}

/* 功能：转换完整的无符号形式十进制文本；参数及返回值见 utils.h。 */
int utils_parse_int(const char *text, int min_value, int max_value, int *value)
{
    char *end; /* strtol 后首个未消费字符。 */
    long parsed; /* 用 long 接收并检查溢出，再缩窄为 int。 */
    if (text == NULL || value == NULL || min_value > max_value) {
        return 0;
    }
    while (isspace((unsigned char)*text)) {
        ++text;
    }
    if (*text < '0' || *text > '9') {
        return 0;
    }
    errno = 0;
    parsed = strtol(text, &end, 10);
    if (errno == ERANGE || parsed < min_value || parsed > max_value) {
        return 0;
    }
    while (isspace((unsigned char)*end)) {
        ++end;
    }
    if (*end != '\0') {
        return 0;
    }
    *value = (int)parsed;
    return 1;
}

/* 功能：刷新提示后安全读取整数；参数及返回值见 utils.h。 */
InputResult utils_read_int_range(const char *prompt, int min_value,
                                int max_value, int *value)
{
    char line[INPUT_LINE_CAPACITY]; /* 本次菜单输入行。 */
    LineResult result; /* 区分可重试输入和无法继续的流错误。 */
    if (fputs(prompt, stdout) == EOF || fflush(stdout) == EOF) {
        return INPUT_ERROR;
    }
    result = utils_read_line(stdin, line, sizeof line);
    if (result == LINE_EOF) {
        return INPUT_EOF;
    }
    if (result == LINE_ERROR) {
        return INPUT_ERROR;
    }
    if (result != LINE_OK ||
        !utils_parse_int(line, min_value, max_value, value)) {
        return INPUT_INVALID;
    }
    return INPUT_OK;
}

/* 功能：有界搜索结尾。参数：text 字符数组；capacity 上限。返回：长度，未终止/NULL 为 capacity。 */
static size_t field_length(const char *text, size_t capacity)
{
    size_t i; /* 待检查字节。 */
    if (text == NULL) {
        return capacity;
    }
    for (i = 0; i < capacity; ++i) {
        if (text[i] == '\0') {
            return i;
        }
    }
    return capacity;
}

/* 功能：校验 ASCII 标识；参数、返回值见 utils.h。 */
int utils_valid_id(const char *text)
{
    size_t length = field_length(text, ID_LEN); /* 不允许截断后接受。 */
    size_t i; /* 当前字节。 */
    if (length == 0 || length == ID_LEN) {
        return 0;
    }
    for (i = 0; i < length; ++i) {
        unsigned char ch = (unsigned char)text[i]; /* 不依赖 locale。 */
        int alnum = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
                    (ch >= '0' && ch <= '9'); /* ASCII 字母数字。 */
        if (!alnum && (i == 0 || (ch != '_' && ch != '-'))) {
            return 0;
        }
    }
    return 1;
}

/* 功能：严格解码 UTF-8 并校验持久字段；参数、返回值见 utils.h。 */
int utils_valid_field(const char *text, size_t capacity)
{
    size_t length = field_length(text, capacity); /* 字节长度。 */
    size_t i = 0; /* 当前序列位置。 */
    if (length == 0 || length == capacity || text[0] == ' ' || text[length - 1] == ' ') {
        return 0;
    }
    while (i < length) {
        unsigned char ch = (unsigned char)text[i++]; /* 首字节。 */
        unsigned long code; /* Unicode 码点。 */
        unsigned long minimum; /* 最小码点，拒绝过长编码。 */
        size_t remaining; /* 尚需的续字节数。 */
        if (ch < 0x80) {
            if (ch < 0x20 || ch == 0x7f || ch == '|') {
                return 0;
            }
            continue;
        }
        if (ch >= 0xc2 && ch <= 0xdf) {
            code = ch & 0x1f; remaining = 1; minimum = 0x80;
        } else if (ch >= 0xe0 && ch <= 0xef) {
            code = ch & 0x0f; remaining = 2; minimum = 0x800;
        } else if (ch >= 0xf0 && ch <= 0xf4) {
            code = ch & 0x07; remaining = 3; minimum = 0x10000;
        } else {
            return 0;
        }
        if (remaining > length - i) {
            return 0;
        }
        while (remaining-- > 0) {
            ch = (unsigned char)text[i++];
            if ((ch & 0xc0) != 0x80) {
                return 0;
            }
            code = (code << 6) | (ch & 0x3f);
        }
        if (code < minimum || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff) ||
            (code >= 0x80 && code <= 0x9f) || code == 0xfeff) {
            return 0;
        }
    }
    return 1;
}

/* 功能：读取完整字段并保护密码回显；参数、返回值见 utils.h。 */
InputResult utils_read_field(const char *prompt, char *buffer, size_t capacity, int secret)
{
    char line[DETAIL_LEN + 2] = {0}; /* 可容纳 255 字节故障说明及 CR/NUL；菜单上限仍为 127。 */
    LineResult result; /* 读行状态。 */
    size_t length; /* 去掉 CR 后的完整字节数。 */
#ifdef _WIN32
    HANDLE input = GetStdHandle(STD_INPUT_HANDLE); /* 控制台或重定向句柄。 */
    DWORD original_mode = 0; /* 原控制台模式。 */
    int changed = 0; /* 仅在关闭回显成功后恢复。 */
#else
    (void)secret;
#endif
    if (buffer == NULL || capacity < 2 || capacity >= sizeof line ||
        fputs(prompt, stdout) == EOF || fflush(stdout) == EOF) {
        return INPUT_ERROR;
    }
    buffer[0] = '\0';
#ifdef _WIN32
    if (secret && GetConsoleMode(input, &original_mode)) {
        if (!SetConsoleMode(input, original_mode & ~ENABLE_ECHO_INPUT)) {
            fputs("无法关闭密码回显，已取消输入。\n", stderr);
            return INPUT_ERROR;
        }
        changed = 1;
    }
#endif
    result = utils_read_line(stdin, line, sizeof line);
#ifdef _WIN32
    if (changed) {
        if (!SetConsoleMode(input, original_mode)) {
            fputs("恢复控制台输入模式失败。\n", stderr);
            result = LINE_ERROR;
        }
        putchar('\n');
    }
#endif
    length = strlen(line); /* read_line 写入结尾，含 NUL 的行仍由 result 拒绝。 */
    if (length > 0 && line[length - 1] == '\r') {
        line[--length] = '\0';
    }
    if (result == LINE_OK && length < capacity && utils_valid_field(line, capacity)) {
        memcpy(buffer, line, length + 1);
        utils_clear_secret(line, sizeof line);
        return INPUT_OK;
    }
    utils_clear_secret(line, sizeof line);
    if (result == LINE_EOF) {
        return INPUT_EOF;
    }
    return result == LINE_ERROR ? INPUT_ERROR : INPUT_INVALID;
}

/* 功能：清除敏感缓冲区；参数、返回值见 utils.h。 */
void utils_clear_secret(void *buffer, size_t size)
{
    volatile unsigned char *cursor = buffer; /* volatile 写不能被死存储消除。 */
    while (size-- > 0) {
        *cursor++ = 0;
    }
}

/* 功能：计算某月天数。参数：year 年；month 合法月份。返回：天数。 */
static int days_in_month(int year, int month)
{
    static const int days[] = {31,28,31,30,31,30,31,31,30,31,30,31}; /* 公历平年。 */
    return days[month - 1] + (month == 2 && year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
}

/* 功能：真实公历验证；参数和返回值见 utils.h。 */
int utils_valid_date(int date)
{
    int year = date / 10000; /* 四位年。 */
    int month = date / 100 % 100; /* 月。 */
    int day = date % 100; /* 日。 */
    return year >= 1000 && year <= 9999 && month >= 1 && month <= 12 &&
        day >= 1 && day <= days_in_month(year, month);
}

/* 功能：公历日序号；参数和返回值见 utils.h。 */
int utils_date_serial(int date)
{
    int year = date / 10000; /* 当前年。 */
    int month; /* 累加此前月份。 */
    int serial; /* 连续天数，最大不到四百万，不会 int 溢出。 */
    if (!utils_valid_date(date)) {
        return -1;
    }
    serial = 365 * (year - 1) + (year - 1) / 4 - (year - 1) / 100 + (year - 1) / 400;
    for (month = 1; month < date / 100 % 100; ++month) {
        serial += days_in_month(year, month);
    }
    return serial + date % 100;
}

/* 功能：按自然日推进，不用 YYYYMMDD 直接加一；参数和返回值见 utils.h。 */
int utils_date_add(int date, int days)
{
    int year = date / 10000, month = date / 100 % 100, day = date % 100; /* 日期分量。 */
    if (!utils_valid_date(date) || days < -36600 || days > 36600) {
        return 0;
    }
    while (days > 0) {
        if (++day > days_in_month(year, month)) {
            day = 1;
            if (++month > 12) { month = 1; ++year; }
        }
        --days;
    }
    while (days < 0) {
        if (--day == 0) {
            if (--month == 0) { month = 12; --year; }
            day = days_in_month(year, month);
        }
        ++days;
    }
    return year >= 1000 && year <= 9999 ? year * 10000 + month * 100 + day : 0;
}

/* 功能：本地系统墙钟读取；参数和返回值见 utils.h。 */
int utils_local_time(void *context, LabTime *now)
{
    time_t timestamp = time(NULL); /* 系统当前时间。 */
    struct tm *local; /* 标准库本地时间，单进程立即复制所需字段。 */
    (void)context;
    if (now == NULL || timestamp == (time_t)-1 || (local = localtime(&timestamp)) == NULL) {
        return 0;
    }
    if (local->tm_year < 1000 - 1900 || local->tm_year > 9999 - 1900) {
        return 0; /* 先限制年份再乘 10000，避免异常系统日期产生有符号溢出。 */
    }
    now->date = (local->tm_year + 1900) * 10000 + (local->tm_mon + 1) * 100 + local->tm_mday;
    now->hour = local->tm_hour;
    now->minute = local->tm_min;
    now->second = local->tm_sec;
    return 1;
}

/* 功能：格式化九个时段；参数和返回值见 utils.h。 */
int utils_format_slot(int slot, char *buffer, size_t capacity)
{
    int written; /* snprintf 需要的字节数。 */
    if (slot < 0 || slot >= SLOT_COUNT || buffer == NULL || capacity == 0) {
        return 0;
    }
    written = snprintf(buffer, capacity, "%02d:00-%02d:00", OPEN_HOUR + slot, OPEN_HOUR + slot + 1);
    return written >= 0 && (size_t)written < capacity;
}

/* 功能：完整墙钟快照验证；参数、返回值见 utils.h。 */
int utils_time_valid(const LabTime *now)
{
    return now != NULL && utils_valid_date(now->date) && now->hour >= 0 && now->hour < 24 &&
        now->minute >= 0 && now->minute < 60 && now->second >= 0 && now->second < 60;
}

/* 功能：基于已确认的固定时刻判断未来时段，供维护重放使用；契约见 utils.h。 */
int utils_slot_future(const LabTime *now, int date, int slot)
{
    if (!utils_time_valid(now) || !utils_valid_date(date) || slot < 0 || slot >= SLOT_COUNT) { return 0; }
    return date > now->date || (date == now->date &&
        (OPEN_HOUR + slot) * 3600 > now->hour * 3600 + now->minute * 60 + now->second);
}
