/* P0 存储集成测试驱动：直接检验保存接口，不向主菜单添加故障管理。 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lab.h"
#include "storage.h"

/* 功能：按测试模式保存快照并验证重读。
 * 参数：argc/argv 包含模式 save、invalid-no、duplicate、invalid-status 或 null。
 * 返回：符合测试预期为 EXIT_SUCCESS，否则 EXIT_FAILURE。
 */
int main(int argc, char **argv)
{
    Lab computers[COMPUTER_COUNT]; /* 待保存的测试快照。 */
    Lab reloaded[COMPUTER_COUNT]; /* 重读结果。 */
    int invalid = 0; /* 当前是否为必须拒绝保存的输入。 */
    if (argc != 2 || !storage_initialize(computers)) {
        return EXIT_FAILURE;
    }
    if (strcmp(argv[1], "null") == 0) {
        return storage_save_computers(NULL) ? EXIT_FAILURE : EXIT_SUCCESS;
    }
    if (strcmp(argv[1], "invalid-no") == 0) {
        computers[0].no = 0;
        invalid = 1;
    } else if (strcmp(argv[1], "duplicate") == 0) {
        computers[0].no = computers[1].no;
        invalid = 1;
    } else if (strcmp(argv[1], "invalid-status") == 0) {
        computers[0].status = (ComputerStatus)2;
        invalid = 1;
    } else if (strcmp(argv[1], "save") == 0) {
        computers[6].status = COMPUTER_FAULT; /* 只为验证持久化提供一个非默认状态。 */
    } else {
        return EXIT_FAILURE;
    }
    if (invalid) {
        return storage_save_computers(computers) ? EXIT_FAILURE : EXIT_SUCCESS;
    }
    if (!storage_save_computers(computers) ||
        storage_load_computers(reloaded) != 1 ||
        reloaded[6].status != COMPUTER_FAULT || !lab_validate_computers(reloaded)) {
        return EXIT_FAILURE;
    }
    puts("snapshot round trip OK");
    return EXIT_SUCCESS;
}
