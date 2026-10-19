#include "lab.h"

#include <stddef.h>

/* 功能：按座位顺序初始化；参数 computers 为输出数组；返回：无。 */
void lab_init_computers(Lab computers[COMPUTER_COUNT])
{
    int i; /* 数组下标，对应电脑编号 i + 1。 */
    for (i = 0; i < COMPUTER_COUNT; ++i) {
        computers[i].no = i + 1;
        computers[i].status = COMPUTER_NORMAL;
    }
}

/* 功能：拒绝重复/缺失编号和非法状态；参数 computers 为输入；返回：有效 1，否则 0。 */
int lab_validate_computers(const Lab computers[COMPUTER_COUNT])
{
    int seen[COMPUTER_COUNT] = {0}; /* 每个编号是否已经出现。 */
    int i; /* 当前记录下标。 */
    if (computers == NULL) {
        return 0;
    }
    for (i = 0; i < COMPUTER_COUNT; ++i) {
        int no = computers[i].no; /* 先验证编号再用作下标，避免越界。 */
        if (no < 1 || no > COMPUTER_COUNT || seen[no - 1] ||
            (computers[i].status != COMPUTER_NORMAL &&
             computers[i].status != COMPUTER_FAULT)) {
            return 0;
        }
        seen[no - 1] = 1;
    }
    return 1;
}

/* 功能：映射基础状态；参数 computer 为有效记录；返回：故障 '!'，正常 '*'。 */
char lab_seat_symbol(const Lab *computer)
{
    return computer->status == COMPUTER_FAULT ? '!' : '*';
}

/* 功能：只负责内存状态变更；参数与返回值见 lab.h。 */
int lab_set_status(Lab computers[COMPUTER_COUNT], int no, ComputerStatus status)
{
    int i; /* 支持完整但乱序的集合，不能把未校验编号直接作下标。 */
    if (no < 1 || no > COMPUTER_COUNT || (status != COMPUTER_NORMAL && status != COMPUTER_FAULT) ||
        !lab_validate_computers(computers)) { return 0; }
    for (i = 0; i < COMPUTER_COUNT; ++i) {
        if (computers[i].no == no) { computers[i].status = status; return 1; }
    }
    return 0;
}
