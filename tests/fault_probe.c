/* P3 测试驱动：固定时钟、实际 stdio 故障及进程 _Exit；不编入正式程序。 */
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "admin.h"
#include "lab.h"
#include "test_clock.h"
#undef fprintf
#undef fflush
#undef fclose

static int tracking, allocations, fail_at, stage, target_stage, injected;
static FILE *writing;
static const char *failure;
void *__real_malloc(size_t size);
void *__real_calloc(size_t count, size_t size);
void *__real_realloc(void *pointer, size_t size);
/* 功能：计数并注入一次实际分配失败。参数：无。返回：失败 1。 */
static int allocation_fails(void)
{
    if (!tracking) { return 0; }
    ++allocations; return fail_at > 0 && allocations == fail_at;
}
void *__wrap_malloc(size_t size) { return allocation_fails() ? NULL : __real_malloc(size); }
void *__wrap_calloc(size_t count, size_t size) { return allocation_fails() ? NULL : __real_calloc(count, size); }
void *__wrap_realloc(void *p, size_t size) { return allocation_fails() ? NULL : __real_realloc(p, size); }
/* 功能：仅对指定第N个快照执行一次故障；参数：kind。返回：命中1。 */
static int hit(const char *kind)
{
    if (!injected && tracking && stage == target_stage && failure != NULL && strcmp(failure, kind) == 0) {
        injected = 1; errno = EIO; return 1;
    }
    return 0;
}
/* 功能：真实格式化写的替身；参数/返回同 fprintf；中断发生在前一目标已经替换之后。 */
int lab_test_fprintf(FILE *stream, const char *format, ...)
{
    va_list args;
    int result;
    if (tracking && stream != stdout && stream != stderr) {
        if (writing == NULL) { writing = stream; ++stage; }
        if (hit("crash")) { _Exit(86); }
        if (hit("write")) { return -1; }
    }
    va_start(args, format); result = vfprintf(stream, format, args); va_end(args); return result;
}
/* 功能：真实刷新失败注入；参数/返回同 fflush。 */
int lab_test_fflush(FILE *stream)
{
    if (writing == stream && hit("flush")) { return EOF; }
    return fflush(stream);
}
/* 功能：实际关闭资源后注入失败；参数/返回同 fclose。 */
int lab_test_fclose(FILE *stream)
{
    int fail = writing == stream && hit("close");
    int result;
    if (writing == stream) { writing = NULL; }
    result = fclose(stream); return fail ? EOF : result;
}
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"Probe check failed at %d\n",__LINE__); return 9; } } while (0)
/* 功能：测试数值参数。参数：text。返回：int，无效INT_MIN。 */
static int number(const char *text)
{
    char *end;
    long value;
    errno = 0; value = strtol(text, &end, 10);
    return errno || *end != '\0' || end == text || value < INT_MIN || value > INT_MAX ? INT_MIN : (int)value;
}
/* 功能：输出只含预约事实的集合。参数：list。返回：无。 */
static void print_bookings(const ReservationList *list)
{
    size_t i;
    printf("COUNT=%zu\n", list->count);
    for (i = 0; i < list->count; ++i) {
        const Reservation *r = &list->items[i];
        printf("ROW=%d,%s,%d,%d,%d,%d\n", r->id,r->student_id,r->computer_no,r->date,r->slot_index,(int)r->status);
    }
}
/* 功能：驱动业务及恢复。参数：mode who a b [detail]。返回：驱动断言状态，与业务码分开。 */
int main(int argc, char **argv)
{
    AuthSession session = {0};
    TestClock clock;
    BookingContext context = {test_clock_now, &clock};
    FaultReport report, before;
    FaultReportList reports = {0};
    ReservationList affected = {0};
    AdminUsage usage;
    AdminStudentInfo student = {0};
    Lab computers[COMPUTER_COUNT];
    const char *mode, *value;
    int a, b;
    char detail[4096] = {0};
    FaultResult result = FAULT_INVALID_INPUT;
    size_t i;
    CHECK(argc >= 3);
    mode = argv[1]; test_clock_init(&clock);
    if (strcmp(argv[2], "admin") == 0) { CHECK(auth_login_admin("Admin", "P3Synthetic!", &session) == AUTH_OK); }
    else if (strcmp(argv[2], "none") == 0) { /* 未登录。 */ }
    else if (strcmp(argv[2], "ghost") == 0) { session = (AuthSession){ROLE_STUDENT,"Missing",1}; }
    else if (strcmp(argv[2], "ghostadmin") == 0) { session = (AuthSession){ROLE_ADMIN,"Missing",1}; }
    else { CHECK(auth_login_student(argv[2], "P3Synthetic!", &session) == AUTH_OK); }
    a = argc > 3 ? number(argv[3]) : 0; b = argc > 4 ? number(argv[4]) : 0;
    tracking = 1;
    value = getenv("LAB_TEST_FAIL_ALLOC"); fail_at = value == NULL ? 0 : number(value);
    value = getenv("LAB_TEST_STAGE"); target_stage = value == NULL ? 1 : number(value);
    failure = getenv("LAB_TEST_IO_FAILURE");
    memset(&report, 0x5a, sizeof report); memcpy(&before, &report, sizeof before);
    if (strcmp(mode, "submit") == 0) {
        /* 十六进制传输避开 Windows 窄 argv 的本地代码页，业务收到原始 UTF-8 字节。 */
        if (argc > 4) {
            size_t length = strlen(argv[4]);
            CHECK(length % 2 == 0 && length / 2 < sizeof detail);
            for (i = 0; i < length / 2; ++i) {
                char pair[3] = {argv[4][2*i], argv[4][2*i+1], '\0'};
                char *end;
                unsigned long byte = strtoul(pair, &end, 16);
                CHECK(*end == '\0' && byte <= 255);
                detail[i] = (char)byte;
            }
        } else { strcpy(detail, "鼠标失灵"); }
        result = fault_submit(&context,&session,a,detail,&report);
    } else if (strcmp(mode,"change") == 0) { result = fault_change_computer(&context,&session,a,(ComputerStatus)b); }
    else if (strcmp(mode,"preview") == 0) { result = fault_preview_change(&context,&session,a,(ComputerStatus)b,&affected); }
    else if (strcmp(mode,"process") == 0) { result = fault_process(&context,&session,a,(FaultAction)b); }
    else if (strcmp(mode,"preview-process") == 0) { result = fault_preview_process(&context,&session,a,(FaultAction)b,&report,&affected); }
    else if (strcmp(mode,"get") == 0) { result = fault_get_report(&session,a,&report); }
    else if (strcmp(mode,"reports") == 0) {
        result = fault_query_reports(&session,a,b,&reports);
        if (result == FAULT_OK) {
            printf("COUNT=%zu\n",reports.count);
            for(i=0;i<reports.count;++i) { const FaultReport *r=&reports.items[i];
                printf("REPORT=%d,%s,%d,%d,%d,%s\n",r->id,r->student_id,r->computer_no,r->date,(int)r->status,r->detail); }
        }
    } else if (strcmp(mode,"usage") == 0) {
        result = admin_query_usage(&session,a,b,&usage);
        if(result==FAULT_OK) {
            printf("COUNT=%zu\nFREE=%d\nFAULTS=%d\nSEATS=",usage.count,usage.seats.free_count,usage.seats.fault_count);
            for(i=0;i<COMPUTER_COUNT;++i) { putchar(usage.seats.symbols[i]); } putchar('\n');
            for(i=0;i<usage.count;++i) { printf("STUDENT=%s,%s,%d\n",usage.rows[i].student_id,usage.rows[i].name,usage.rows[i].computer_no); }
        }
    } else if (strcmp(mode,"student") == 0) {
        result=admin_query_student(&session,argc>3?argv[3]:"U1",(BookingQuery)b,&student);
        if(result==FAULT_OK) { printf("IDENTITY=%s,%s\nACTIVE=%zu\nHISTORY=%zu\n",student.student_id,student.name,student.active_count,student.history_count); print_bookings(&student.records); }
    } else if(strcmp(mode,"recover")==0) { result=fault_from_storage(storage_recover_maintenance()); }
    else if(strcmp(mode,"initialize")==0) { result=storage_initialize(computers)?FAULT_OK:FAULT_STORAGE_ERROR; }
    else if(strcmp(mode,"load")==0) {
        tracking=0; CHECK(storage_reserve_reports(&reports,1)==STORAGE_OK);
        reports.items[0]=(FaultReport){777,"U1",1,20000101,"旧报告",REPORT_PROCESSED}; reports.count=1;
        tracking=1;
        result=fault_from_storage(storage_load_reports(&reports));
        if(result!=FAULT_OK) { CHECK(reports.count==1 && reports.items[0].id==777); }
        printf("COUNT=%zu\n",reports.count);
    } else if(strcmp(mode,"booking")==0) {
        Reservation created;
        printf("BOOKING=%d\n",(int)booking_create(&context,&session,20261020,3,a,&created)); result=FAULT_OK;
    } else if(strcmp(mode,"save-gates")==0) {
        AccountRecords *accounts=calloc(1,sizeof *accounts);
        CHECK(accounts!=NULL); lab_init_computers(computers);
        CHECK(!storage_save_computers(computers));
        CHECK(!storage_save_accounts(ROLE_STUDENT,accounts,0));
        CHECK(storage_save_reservations(&affected)==STORAGE_RECOVERY_REQUIRED);
        CHECK(storage_save_reports(&reports)==STORAGE_RECOVERY_REQUIRED);
        free(accounts); result=FAULT_OK;
    }
    if(result!=FAULT_OK) { CHECK(memcmp(&report,&before,sizeof report)==0); }
    if(result==FAULT_OK && (strcmp(mode,"submit")==0 || strcmp(mode,"get")==0 || strcmp(mode,"preview-process")==0)) { printf("ID=%d\n",report.id); }
    if(result==FAULT_OK && (strcmp(mode,"preview")==0 || strcmp(mode,"preview-process")==0)) { print_bookings(&affected); }
    if(result!=FAULT_OK) { CHECK(affected.count==0 && affected.items==NULL && student.records.items==NULL); }
    printf("RESULT=%d\nALLOCATIONS=%d\nSTAGE=%d\nINJECTED=%d\n",(int)result,allocations,stage,injected);
    storage_free_reports(&reports); storage_free_reservations(&affected); admin_free_student(&student);
    auth_logout(&session); return 0;
}
