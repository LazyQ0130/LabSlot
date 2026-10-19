#include <stdio.h>
#include <stdlib.h>
#include "model.h"
#include "storage.h"
#include "ui.h"

/* 功能：配置终端、加载电脑、运行含身份认证的菜单并检查输出；参数：无；返回：退出码。 */
int main(void)
{
    Lab computers[COMPUTER_COUNT]; /* 只装载 P0 需要的电脑，不分配后续阶段的大数组。 */
    int success; /* 主循环是否正常结束。 */
    ui_initialize_console();
    if (!storage_initialize(computers)) {
        fputs("启动失败：请检查上述数据错误及未完成维护日志。\n", stderr);
        return EXIT_FAILURE;
    }
    puts("数据目录：当前工作目录下的 data/（请从项目根目录启动）。");
    success = ui_run(computers);
    /* 电脑只读；账户在注册当时保存，退出时不重复写文件。 */
    if (fflush(stdout) == EOF || ferror(stdout)) {
        fputs("输出流写入失败。\n", stderr);
        success = 0;
    }
    return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
