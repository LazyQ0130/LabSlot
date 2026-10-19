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

/* 功能：检查/创建数据目录与文件并加载电脑；只有缺失或零字节时初始化。
 * 参数：computers 为输出数组，失败时保持不变。返回：成功 1，失败 0（stderr 有原因）。
 * 初始化仅检查其他五个文件可读；账户由认证操作按角色解析，不清空已有记录。
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

#endif
