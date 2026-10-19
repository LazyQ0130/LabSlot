#ifndef SMART_LAB_UTILS_H
#define SMART_LAB_UTILS_H

#include <stddef.h>
#include <stdio.h>

#define INPUT_LINE_CAPACITY 128 /* 单行最大 127 字节，不含换行。 */

/* 有界读行的结果，区分 EOF、非法字节与 I/O 错误。 */
typedef enum {
    LINE_OK,       /* 已取得一行，可为未带换行的最后一行。 */
    LINE_EOF,      /* 还没读取字节即到输入结束。 */
    LINE_TOO_LONG, /* 已丢弃该行剩余部分，可继续读取下一行。 */
    LINE_INVALID,  /* 含 NUL 字节，已丢弃整行。 */
    LINE_ERROR     /* 读取失败或参数无效。 */
} LineResult;

/* 菜单数字读取结果。 */
typedef enum {
    INPUT_OK,      /* 成功。 */
    INPUT_EOF,     /* 输入正常结束。 */
    INPUT_INVALID, /* 非法输入，本行已消费。 */
    INPUT_ERROR    /* 输入/提示输出失败，调用方应退出。 */
} InputResult;

/* 功能：有界读行并消费整行（含超长行）。参数：stream 输入流；buffer 输出；
 * capacity 为含结尾的容量，至少 2。返回：LineResult。不关闭 stream。
 */
LineResult utils_read_line(FILE *stream, char *buffer, size_t capacity);

/* 功能：严格解析十进制整数，可有首尾空白，拒绝正负号、溢出和尾随非空白。
 * 参数：text 输入；min_value/max_value 闭区间；value 成功时输出。
 * 返回：成功 1，否则 0；失败不修改 value。
 */
int utils_parse_int(const char *text, int min_value, int max_value, int *value);

/* 功能：提示并读取菜单整数。参数：prompt 提示；min/max_value 范围；value 输出。
 * 返回：InputResult；调用方负责对非法输入给出提示。
 */
InputResult utils_read_int_range(const char *prompt, int min_value,
                                int max_value, int *value);

/* 功能：标识首字符为 ASCII 字母/数字，其余允许字母/数字/_/-。
 * 参数：text 为 ID_LEN 容量内的完整字符串。返回：有效 1，否则 0。
 */
int utils_valid_id(const char *text);

/* 功能：校验有界 UTF-8，拒绝空值、非法编码、控制符、|、首尾 ASCII 空格。
 * 参数：text 字段；capacity 为含 NUL 的容量。返回：有效 1，否则 0。
 */
int utils_valid_field(const char *text, size_t capacity);

/* 功能：读取字段，不接受截断；Windows 控制台密码关闭回显后恢复。
 * 参数：prompt 提示；buffer 输出；capacity 含结尾；secret 非零表示密码。
 * 返回：InputResult；去掉 CRLF 的 CR，非法或超长行整行消费。
 */
InputResult utils_read_field(const char *prompt, char *buffer, size_t capacity, int secret);

/* 功能：volatile 写清除敏感缓冲区。参数：buffer 地址；size 字节数。返回：无。 */
void utils_clear_secret(void *buffer, size_t size);

#endif
