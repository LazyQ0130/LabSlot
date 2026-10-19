/* Reuse the original fault-injection harness without changing its assertions. */
#define main p2_original_main
#include "booking_probe.c"
#undef main
#include "recommend.h"
int main(int argc, char **argv)
{
    AuthSession session={0};
    TestClock clock;
    BookingContext context={test_clock_now,&clock};
    Recommendation result, before;
    BookingAvailability view, old_view;
    BookingResult status;
    const char *failure=getenv("LAB_TEST_FAIL_ALLOC");
    size_t i;
    CHECK(argc>=7);
    test_clock_init(&clock);
    if(!strcmp(argv[2],"admin")) { CHECK(auth_login_admin("Admin","P2Synthetic!",&session)==AUTH_OK); }
    else if(!strcmp(argv[2],"none")) { /* no session */ }
    else if(!strcmp(argv[2],"ghost")) { session=(AuthSession){ROLE_STUDENT,"Missing",1}; }
    else { CHECK(auth_login_student("U1","P2Synthetic!",&session)==AUTH_OK); }
    memset(&result,0x5a,sizeof result); before=result;
    memset(&view,0x5a,sizeof view); old_view=view;
    tracking=1; fail_at=failure ? atoi(failure) : 0;
    if(!strcmp(argv[1],"mask")) {
        status=booking_availability(&context,&session,atoi(argv[3]),&view);
        if(status==BOOKING_OK) {
            for(i=0;i<COMPUTER_COUNT;++i) { printf("MASK=%u,%d\n",view.masks[i],view.loads[i]); }
        } else { CHECK(!memcmp(&view,&old_view,sizeof view)); }
    } else {
        status=recommend_seats(&context,&session,atoi(argv[3]),atoi(argv[4]),atoi(argv[5]),(RecommendPreference)atoi(argv[6]),&result);
        if(status==BOOKING_OK) {
            printf("COUNT=%zu\n",result.count);
            for(i=0;i<result.count;++i) {
                SeatRecommendation *r=&result.seats[i];
                printf("ROW=%d,%d,%d,%d,%d,%d\n",r->computer_no,r->score,r->continuity,r->position,r->balance,r->run_length);
            }
        } else { CHECK(!memcmp(&result,&before,sizeof result)); }
    }
    if(!strcmp(argv[1],"stale") && status==BOOKING_OK) {
        AuthSession other={0}; Reservation created;
        CHECK(auth_login_student("U2","P2Synthetic!",&other)==AUTH_OK);
        CHECK(booking_create(&context,&other,atoi(argv[3]),atoi(argv[4]),result.seats[0].computer_no,&created)==BOOKING_OK);
        status=booking_create(&context,&session,atoi(argv[3]),atoi(argv[4]),result.seats[0].computer_no,&created);
    }
    printf("RESULT=%d\nALLOC=%d\n",(int)status,allocations); return 0;
}
