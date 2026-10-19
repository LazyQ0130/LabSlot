#include <stdlib.h>
#include <stdio.h>
#include "storage.h"
#include "ui.h"
#include "test_clock.h"

/* 功能：固定时间 UI 集成入口。参数：无。返回：进程状态；不编入正式可执行文件。 */
int main(void)
{
    Lab computers[COMPUTER_COUNT]; /* 隔离目录电脑。 */
    TestClock clock; /* 测试时钟。 */
    BookingContext context = {test_clock_now, &clock}; /* 显式注入。 */
    int success; /* UI 与输出状态。 */
    test_clock_init(&clock);
    if (!storage_initialize(computers)) { return EXIT_FAILURE; }
    success = ui_run_with_context(computers, &context);
    return success && fflush(stdout) == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
