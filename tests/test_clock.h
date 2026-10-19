#ifndef LABSLOT_TEST_CLOCK_H
#define LABSLOT_TEST_CLOCK_H
#include <stdlib.h>
#include <stdio.h>
#include "booking.h"

/* 仅测试程序使用的时钟状态，不编入正式 main。 */
typedef struct {
    LabTime first; /* 第一次读取时刻。 */
    LabTime later; /* 可选的后续时刻。 */
    int reads;    /* 已读取次数。 */
    int advance;  /* 非零时从第二次读取切换。 */
    int broken;   /* 非零模拟时间源失败。 */
} TestClock;

/* 功能：解析测试环境时钟。参数：clock 输出。返回：无。环境由测试脚本提供合法 int 文本。 */
static void test_clock_init(TestClock *clock)
{
    const char *now = getenv("LAB_TEST_NOW"); /* 仅测试驱动支持环境注入。 */
    const char *later = getenv("LAB_TEST_LATER"); /* 模拟确认/保存边界时间前进。 */
    *clock = (TestClock){{20261019,10,15,0},{20261019,11,0,0},0,0,0};
    if (now != NULL) { sscanf(now, "%d,%d,%d,%d", &clock->first.date, &clock->first.hour, &clock->first.minute, &clock->first.second); }
    if (later != NULL) {
        sscanf(later, "%d,%d,%d,%d", &clock->later.date, &clock->later.hour, &clock->later.minute, &clock->later.second);
        clock->advance = 1;
    }
    clock->broken = getenv("LAB_TEST_CLOCK_ERROR") != NULL;
}

/* 功能：返回固定/推进时钟。参数：context 为 TestClock；now 输出。返回：成功 1，否则 0。 */
static int test_clock_now(void *context, LabTime *now)
{
    TestClock *clock = context; /* 测试自有上下文。 */
    *now = clock->advance && clock->reads++ > 0 ? clock->later : clock->first;
    return !clock->broken;
}
#endif
