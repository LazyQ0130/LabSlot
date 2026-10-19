#define main p2_original_main
#include "booking_probe.c"
#undef main
#include "heatmap.h"
int main(int argc, char **argv)
{
    AuthSession session={0}; TestClock clock;
    BookingContext context={test_clock_now,&clock};
    BookingHeatmap result,before;
    StatsResult status;
    const char *failure=getenv("LAB_TEST_FAIL_ALLOC");
    int d,s;
    CHECK(argc>=3); test_clock_init(&clock);
    if(!strcmp(argv[1],"admin")) { CHECK(auth_login_admin("Admin","P2Synthetic!",&session)==AUTH_OK); }
    else if(!strcmp(argv[1],"student")) { CHECK(auth_login_student("U1","P2Synthetic!",&session)==AUTH_OK); }
    else if(!strcmp(argv[1],"ghost")) { session=(AuthSession){ROLE_STUDENT,"Missing",1}; }
    memset(&result,0x5a,sizeof result); before=result;
    tracking=1; fail_at=failure ? atoi(failure) : 0;
    status=heatmap_compute(&context,&session,atoi(argv[2]),&result);
    if(status==STATS_OK) {
        for(d=0;d<7;++d) { for(s=0;s<9;++s) {
            HeatCell *cell=&result.cells[d][s];
            printf("CELL=%d,%d,%d,%d\n",result.dates[d],s,cell->count,(int)(cell->utilization*100));
        } }
        if(session.role==ROLE_ADMIN) {
            WeeklyStats week;
            CHECK(stats_compute_week(&context,&session,atoi(argv[2]),&week)==STATS_OK);
            for(d=0;d<7;++d) { for(s=0;s<9;++s) { CHECK(result.cells[d][s].count==week.days[d].counts.slot_counts[s] && result.cells[d][s].utilization==week.days[d].slot_utilization[s]); } }
        }
    } else { CHECK(!memcmp(&result,&before,sizeof result)); }
    for(s=-1;s<=41;++s) { printf("LEVEL=%d,%d\n",s,heatmap_level(s)); }
    printf("RESULT=%d\nALLOC=%d\n",status,allocations); return 0;
}
