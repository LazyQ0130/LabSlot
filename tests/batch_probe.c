#define main p2_original_main
#include "booking_probe.c"
#undef main
#include "stats.h"
static int clock_reads;
static int reservation_loads, reservation_saves;
StorageResult __real_storage_load_reservations(ReservationList *);
StorageResult __real_storage_save_reservations(const ReservationList *);
StorageResult __wrap_storage_load_reservations(ReservationList *list)
{
    if(tracking) { ++reservation_loads; } return __real_storage_load_reservations(list);
}
StorageResult __wrap_storage_save_reservations(const ReservationList *list)
{
    if(tracking) { ++reservation_saves; } return __real_storage_save_reservations(list);
}
static int batch_now(void *context,LabTime *now) { ++clock_reads; return test_clock_now(context,now); }
int main(int argc,char **argv)
{
    AuthSession session={0}; TestClock clock;
    BookingContext context={batch_now,&clock};
    BatchBooking result,before;
    ReservationList list={0};
    BookingResult status;
    const char *failure=getenv("LAB_TEST_FAIL_ALLOC");
    int i;
    CHECK(argc>=7);test_clock_init(&clock);
    if(!strcmp(argv[2],"admin")) { CHECK(auth_login_admin("Admin","P2Synthetic!",&session)==AUTH_OK); }
    else if(!strcmp(argv[2],"none")) { /* no session */ }
    else if(!strcmp(argv[2],"ghost")) { session=(AuthSession){ROLE_STUDENT,"Missing",1}; }
    else { CHECK(auth_login_student("U1","P2Synthetic!",&session)==AUTH_OK); }
    memset(&result,0x5a,sizeof result);before=result;
    tracking=1;fail_at=failure ? atoi(failure) : 0;io_failure=getenv("LAB_TEST_IO");
    if(!strcmp(argv[1],"check")) { status=booking_check_batch(&context,&session,atoi(argv[3]),atoi(argv[4]),atoi(argv[5]),atoi(argv[6])); }
    else if(!strcmp(argv[1],"cancel")) { status=booking_cancel(&context,&session,atoi(argv[3])); }
    else if(!strcmp(argv[1],"list")) {
        status=booking_query(&context,&session,BOOKING_QUERY_ALL,&list);
        if(status==BOOKING_OK) { for(i=0;i<(int)list.count;++i) { Reservation *r=&list.items[i];printf("ROW=%d,%d,%d,%d,%d\n",r->id,r->computer_no,r->date,r->slot_index,r->status); } }
    } else {
        status=booking_create_batch(&context,&session,atoi(argv[3]),atoi(argv[4]),atoi(argv[5]),atoi(argv[6]),&result);
        if(status==BOOKING_OK) {
            printf("COUNT=%d\n",result.count);
            for(i=0;i<result.count;++i) {
                Reservation *r=&result.records[i];CHECK(!strcmp(r->student_id,session.user_id));
                printf("ROW=%d,%d,%d,%d,%d\n",r->id,r->computer_no,r->date,r->slot_index,r->status);
            }
        }
    }
    if(status!=BOOKING_OK) { CHECK(!memcmp(&result,&before,sizeof result)); }
    tracking=0;storage_free_reservations(&list);
    printf("RESULT=%d\nALLOC=%d\nREADS=%d\nLOADS=%d\nSAVES=%d\n",status,allocations,clock_reads,reservation_loads,reservation_saves);return 0;
}
