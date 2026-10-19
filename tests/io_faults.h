#ifndef LABSLOT_TEST_IO_FAULTS_H
#define LABSLOT_TEST_IO_FAULTS_H
/* 仅 API 测试编译时 -include；先读标准头再重定向，避开 MinGW 内联/导入别名。 */
#include <stdio.h>
/* 功能/参数/返回与标准 fprintf 相同，测试驱动可注入文件写失败。 */
int lab_test_fprintf(FILE *stream, const char *format, ...);
/* 功能/参数/返回与标准 fflush 相同，测试驱动可注入文件刷新失败。 */
int lab_test_fflush(FILE *stream);
/* 功能/参数/返回与标准 fclose 相同，测试驱动可注入文件关闭失败。 */
int lab_test_fclose(FILE *stream);
#define fprintf lab_test_fprintf
#define fflush lab_test_fflush
#define fclose lab_test_fclose
#endif
