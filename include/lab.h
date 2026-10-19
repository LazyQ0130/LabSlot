#ifndef SMART_LAB_LAB_H
#define SMART_LAB_LAB_H

#include "model.h"

/* 功能：生成默认的 40 台正常电脑。参数：computers 为可写数组。返回：无。 */
void lab_init_computers(Lab computers[COMPUTER_COUNT]);

/* 功能：校验完整电脑集合。参数：computers 为只读数组。返回：有效 1，否则 0。 */
int lab_validate_computers(const Lab computers[COMPUTER_COUNT]);

/* 功能：取得基础状态符号。参数：computer 为有效电脑。返回：正常 '*'、故障 '!'。 */
char lab_seat_symbol(const Lab *computer);

#endif
