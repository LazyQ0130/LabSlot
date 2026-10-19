#define main p2_original_main
#include "booking_probe.c"
#undef main
#include "health.h"
static int report_loads,computer_loads;
StorageResult __real_storage_load_reports(FaultReportList *);
int __real_storage_load_computers(Lab *);
StorageResult __wrap_storage_load_reports(FaultReportList *list)
{
    if(tracking) { ++report_loads; }return __real_storage_load_reports(list);
}
int __wrap_storage_load_computers(Lab *computers)
{
    if(tracking) { ++computer_loads; }return __real_storage_load_computers(computers);
}
int main(int argc,char **argv)
{
    AuthSession session={0};TestClock clock;
    BookingContext context={test_clock_now,&clock};
    HealthSummary result,before;
    FaultResult status;
    const char *failure=getenv("LAB_TEST_FAIL_ALLOC");
    int i;
    CHECK(argc>=2);test_clock_init(&clock);
    if(!strcmp(argv[1],"admin")) { CHECK(auth_login_admin("Admin","P2Synthetic!",&session)==AUTH_OK); }
    else if(!strcmp(argv[1],"student")) { CHECK(auth_login_student("U1","P2Synthetic!",&session)==AUTH_OK); }
    else if(!strcmp(argv[1],"ghost")) { session=(AuthSession){ROLE_ADMIN,"Missing",1}; }
    memset(&result,0x5a,sizeof result);before=result;
    tracking=1;fail_at=failure ? atoi(failure) : 0;
    if(argc>2 && !strcmp(argv[2],"detail")) {
        FaultReport report,old;
        memset(&report,0x5a,sizeof report);old=report;
        status=fault_get_report(&session,1,&report);
        CHECK(status==FAULT_OK || !memcmp(&report,&old,sizeof report));
    } else { status=health_compute(&context,&session,&result); }
    if(status==FAULT_OK) {
        printf("GROUP=%d,%d,%d,%d\n",result.level_counts[0],result.level_counts[1],result.level_counts[2],result.level_counts[3]);
        for(i=0;i<COMPUTER_COUNT;++i) {
            DeviceHealth *r=&result.devices[i];
            printf("ROW=%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%u\n",r->computer_no,r->status,r->score,r->level,
                r->recent_7,r->days_8_30,r->older,r->pending,r->total_reports,r->report_penalty,r->fault_penalty,r->repeat_penalty,r->warnings);
        }
    } else { CHECK(!memcmp(&result,&before,sizeof result)); }
    for(i=-1;i<=101;++i) { printf("LEVEL=%d,%d\n",i,health_level(i)); }
    printf("RESULT=%d\nALLOC=%d\nLOADS=%d,%d\n",status,allocations,computer_loads,report_loads);return 0;
}
