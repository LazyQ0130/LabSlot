#ifndef SMART_LAB_STORAGE_H
#define SMART_LAB_STORAGE_H

#include "model.h"
#include <stddef.h>

/* 路径相对于进程工作目录；应先 cd 到项目根目录再启动。 */
#define DATA_DIRECTORY "data"
#define ADMINS_PATH DATA_DIRECTORY "/admins.txt"
#define STUDENTS_PATH DATA_DIRECTORY "/students.txt"
#define COMPUTERS_PATH DATA_DIRECTORY "/computers.txt"
#define RESERVATIONS_PATH DATA_DIRECTORY "/reservations.txt"
#define REPORTS_PATH DATA_DIRECTORY "/error.txt"
#define COUNTS_PATH DATA_DIRECTORY "/count.txt"
#define COMPUTERS_TEMP_PATH DATA_DIRECTORY "/computers.txt.tmp"
#define ADMINS_TEMP_PATH DATA_DIRECTORY "/admins.txt.tmp"
#define STUDENTS_TEMP_PATH DATA_DIRECTORY "/students.txt.tmp"
#define RESERVATIONS_TEMP_PATH DATA_DIRECTORY "/reservations.txt.tmp"

/* 功能：检查/创建数据目录与文件并加载电脑；只有缺失或零字节时初始化。
 * 参数：computers 为输出数组，失败时保持不变。返回：成功 1，失败 0（stderr 有原因）。
 * 初始化仅检查其他五个文件可读；账户/预约由各业务按需解析，不清空已有记录。
 */
int storage_initialize(Lab computers[COMPUTER_COUNT]);

/* 功能：严格读取 40 条电脑记录并按编号排序；绝不写文件。
 * 参数：computers 为输出数组，失败时保持不变。
 * 返回：完整有效 1，零字节 0，缺失/损坏/I/O 错误 -1。
 */
int storage_load_computers(Lab computers[COMPUTER_COUNT]);

/* 功能：校验后先写临时文件再替换目标；不执行故障管理等业务。
 * 参数：computers 为完整有效的只读数组。返回：成功 1，失败 0。
 * 限单进程使用；失败可能保留 .tmp 供人工检查，绝不删除旧 computers.txt。
 */
int storage_save_computers(const Lab computers[COMPUTER_COUNT]);

/* 功能：按角色严格读取，检查字段、UTF-8、重复和容量，不写文件。
 * 参数：role 管理员/学生；accounts 为堆上输出集合；count 为条数输出。
 * 返回：成功 1（空文件为零条），失败 0。失败保持两个输出不变。
 * 错误只报告路径/行号；运行中缺失文件不当作空表。须先 storage_initialize。
 */
int storage_load_accounts(UserRole role, AccountRecords *accounts, size_t *count);

/* 功能：完整校验后复用独占临时文件和替换逻辑。
 * 参数：role 决定 accounts 有效成员；count 条数。返回：成功 1，失败 0。
 * 失败保留原文件，可能留下 .tmp；单进程使用，调用方负责先加载保留旧记录。
 */
int storage_save_accounts(UserRole role, const AccountRecords *accounts, size_t count);

/* 预约存储结果，细分内存不足与超容量，不能将失败当空集合。 */
typedef enum {
    STORAGE_OK = 0,       /* 成功。 */
    STORAGE_ERROR,        /* I/O、参数或损坏数据。 */
    STORAGE_NO_MEMORY,    /* 动态内存不足，原对象不变。 */
    STORAGE_CAPACITY     /* 条数超过上限。 */
} StorageResult;

/* 功能：安全扩容（含 realloc 失败和乘法溢出检查）。参数：list 有效集合；needed 所需条数。
 * 返回：StorageResult；失败不改变 list，绝不缩容或修改 count。
 */
StorageResult storage_reserve_reservations(ReservationList *list, size_t needed);

/* 功能：释放并清零集合。参数：list 可为 NULL。返回：无。 */
void storage_free_reservations(ReservationList *list);

/* 功能：完整验证字段、编号唯一及活跃座位/学生冲突，复杂度 O(n log n)。
 * 参数：list 只读有效内存集合。返回：StorageResult；不改变集合或文件。
 */
StorageResult storage_validate_reservations(const ReservationList *list);

/* 功能：严格加载预约，完整成功后才替换输出。参数：list 已零初始化或有效的自有集合。
 * 返回：StorageResult；失败不改变原集合，也不写文件。缺失文件是错误，零字节是合法空表。
 */
StorageResult storage_load_reservations(ReservationList *list);

/* 功能：完整验证后复用独占临时快照保存。参数：list 只读集合。返回：StorageResult。
 * 失败不先删除原文件，可能保留 .tmp；不得直接将失败返回值当成功。
 */
StorageResult storage_save_reservations(const ReservationList *list);

#endif
