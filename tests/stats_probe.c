/* P4 测试驱动：固定时钟、实际 stdio 故障及进程 _Exit；不编入正式程序。 */
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "stats.h"
#include "fault.h"
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

/* 功能：输出不含个人信息的每日指标。参数：daily；返回：无。 */
static void print_daily(const DailyStats *daily)
{
    int i;
    const DailyCount *c = &daily->counts;
    printf("DAY=%d,%d,%d,%d,%d,%.8f",c->date,c->reservation_count,c->student_count,
           c->reserved_hours,daily->completed_hours,daily->utilization);
    for(i=0;i<SLOT_COUNT;++i) { printf(",%d",c->slot_counts[i]); }
    putchar('\n');
    printf("RATES=");
    for(i=0;i<SLOT_COUNT;++i) { printf("%s%.8f",i==0?"":",",daily->slot_utilization[i]); }
    putchar('\n');
}

/* 功能：驱动统计、存储、故障恢复。参数：mode who date。返回：驱动断言状态。 */
int main(int argc,char **argv)
{
    AuthSession session={0};
    TestClock clock;
    BookingContext context={test_clock_now,&clock};
    DailyStats daily, old_daily;
    WeeklyStats week, old_week;
    StudentStats student, old_student;
    DailyCountList counts={0};
    const char *mode,*value;
    StatsResult result=STATS_INVALID_INPUT;
    int date,i;
    CHECK(argc>=3); mode=argv[1]; test_clock_init(&clock);
    if(strcmp(argv[2],"none")==0) { /* 保留空会话。 */ }
    else if(strcmp(argv[2],"ghost")==0) { session=(AuthSession){ROLE_STUDENT,"Missing",1}; }
    else if(strcmp(argv[2],"ghostadmin")==0) { session=(AuthSession){ROLE_ADMIN,"Missing",1}; }
    else if(strcmp(argv[2],"rawadmin")==0) { session=(AuthSession){ROLE_ADMIN,"Admin",1}; }
    else if(strcmp(argv[2],"rawstudent")==0) { session=(AuthSession){ROLE_STUDENT,"U1",1}; }
    else if(strcmp(argv[2],"admin")==0) { CHECK(auth_login_admin("Admin","P4Synthetic!",&session)==AUTH_OK); }
    else { CHECK(auth_login_student(argv[2],"P4Synthetic!",&session)==AUTH_OK); }
    date=argc>3?number(argv[3]):20261019;
    memset(&daily,0x5a,sizeof daily);memcpy(&old_daily,&daily,sizeof daily);
    memset(&week,0x5a,sizeof week);memcpy(&old_week,&week,sizeof week);
    memset(&student,0x5a,sizeof student);memcpy(&old_student,&student,sizeof student);
    tracking=1;
    value=getenv("LAB_TEST_FAIL_ALLOC");fail_at=value==NULL?0:number(value);
    value=getenv("LAB_TEST_STAGE");target_stage=value==NULL?1:number(value);
    failure=getenv("LAB_TEST_IO_FAILURE");
    if(strcmp(mode,"daily")==0) { result=stats_compute_daily(&context,&session,date,&daily); }
    else if(strcmp(mode,"week")==0) { result=stats_compute_week(&context,&session,date,&week); }
    else if(strcmp(mode,"student")==0) { result=stats_query_student(&context,&session,&student); }
    else if(strcmp(mode,"export")==0) { result=stats_export_daily(&context,&session,date,&daily); }
    else if(strcmp(mode,"null-output")==0) {
        CHECK(stats_compute_daily(&context,&session,date,NULL)==STATS_INVALID_INPUT);
        CHECK(stats_compute_week(&context,&session,date,NULL)==STATS_INVALID_INPUT);
        CHECK(stats_query_student(&context,&session,NULL)==STATS_INVALID_INPUT);
        CHECK(stats_export_daily(&context,&session,date,NULL)==STATS_INVALID_INPUT);
        result=STATS_OK;
    } else if(strcmp(mode,"load-counts")==0) {
        DailyCount *old;
        StorageResult loaded;
        tracking=0;CHECK(storage_reserve_daily_counts(&counts,1)==STORAGE_OK);
        counts.items[0]=(DailyCount){20000101,0,0,0,{0}};counts.count=1;
        old=counts.items;tracking=1;
        loaded=storage_load_daily_counts(&counts);
        if(loaded!=STORAGE_OK) { CHECK(counts.items==old && counts.count==1 && counts.items[0].date==20000101); }
        printf("STORAGE=%d\nCOUNT=%zu\n",(int)loaded,counts.count);
        result=loaded==STORAGE_OK?STATS_OK:(loaded==STORAGE_NO_MEMORY?STATS_NO_MEMORY:(loaded==STORAGE_CAPACITY?STATS_CAPACITY:STATS_STORAGE_ERROR));
    } else if(strcmp(mode,"bad-save")==0) {
        int kind=argc>4?number(argv[4]):0;
        CHECK(storage_reserve_daily_counts(&counts,2)==STORAGE_OK);
        counts.items[0]=(DailyCount){20261019,1,1,1,{1}};counts.count=1;
        if(kind==0) { counts.items[0].date=20260230; }
        else if(kind==1) { counts.items[0].slot_counts[0]=41; }
        else if(kind==2) { counts.items[0].reserved_hours=2; }
        else if(kind==3) { counts.items[0].student_count=2; }
        else if(kind==4) { counts.items[0].student_count=0; }
        else if(kind==5) { counts.items[0].reservation_count=2147483647; }
        else if(kind==6) { counts.items[1]=counts.items[0];counts.count=2; }
        else if(kind==7) { counts.items[1]=counts.items[0];counts.items[1].date=20261018;counts.count=2; }
        CHECK(storage_save_daily_counts(&counts)!=STORAGE_OK);result=STATS_OK;
    } else if(strcmp(mode,"count-gate")==0) {
        CHECK(storage_save_daily_counts(&counts)==STORAGE_RECOVERY_REQUIRED);result=STATS_OK;
    } else if(strcmp(mode,"recover")==0) {
        result=storage_recover_maintenance()==STORAGE_OK?STATS_OK:STATS_RECOVERY_REQUIRED;
    } else if(strcmp(mode,"cancel")==0) {
        printf("BOOKING=%d\n",(int)booking_cancel(&context,&session,date));result=STATS_OK;
    } else if(strcmp(mode,"crash-maintenance")==0) {
        printf("FAULT=%d\n",(int)fault_change_computer(&context,&session,1,COMPUTER_FAULT));result=STATS_OK;
    }
    if(result!=STATS_OK) {
        CHECK(memcmp(&daily,&old_daily,sizeof daily)==0);
        CHECK(memcmp(&week,&old_week,sizeof week)==0);
        CHECK(memcmp(&student,&old_student,sizeof student)==0);
    } else if(strcmp(mode,"daily")==0 || strcmp(mode,"export")==0) { print_daily(&daily); }
    else if(strcmp(mode,"week")==0) {
        printf("WEEK=%d,%d,%d,%d,%.8f,%.8f\n",week.start_date,week.end_date,week.reservation_count,
               week.student_count,week.average_daily_hours,week.utilization);
        for(i=0;i<7;++i) { print_daily(&week.days[i]); }
    } else if(strcmp(mode,"student")==0) {
        printf("IDENTITY=%s,%s\nSTUDENT=%d,%d,%d,%d,%d\n",student.student_id,student.name,
               student.completed_hours,student.active_count,student.completed_count,student.pending_count,student.cancelled_count);
    }
    printf("RESULT=%d\nALLOCATIONS=%d\nSTAGE=%d\nINJECTED=%d\n",(int)result,allocations,stage,injected);
    storage_free_daily_counts(&counts);auth_logout(&session);return 0;
}
