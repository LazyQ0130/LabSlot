#include "recommend.h"
#include <stdlib.h>

const int *recommend_weights(RecommendPreference preference)
{
    static const int weights[3][3]={{70,15,15},{20,70,10},{20,10,70}};
    return preference>=RECOMMEND_CONTINUITY && preference<=RECOMMEND_LOAD ? weights[preference] : NULL;
}
static int order(const void *left, const void *right)
{
    const SeatRecommendation *a=left, *b=right;
    if (a->score!=b->score) { return (a->score<b->score)-(a->score>b->score); }
    return (a->computer_no>b->computer_no)-(a->computer_no<b->computer_no);
}
BookingResult recommend_seats(const BookingContext *context, const AuthSession *session,
                              int date, int start, int duration, RecommendPreference preference,
                              Recommendation *out)
{
    BookingAvailability view;
    Recommendation result={0};
    SeatRecommendation candidates[COMPUTER_COUNT];
    unsigned int request=booking_range_mask(start,duration);
    const int *weights=recommend_weights(preference);
    BookingResult status;
    size_t used=0, i;
    int no;
    if (!auth_session_has_role(session,ROLE_STUDENT)) { return BOOKING_UNAUTHORIZED; }
    if (!out || !request || !weights) { return BOOKING_INVALID_INPUT; }
    status=booking_availability(context,session,date,&view);
    if (status!=BOOKING_OK) { return status; }
    if (!utils_slot_future(&view.as_of,date,start)) { return BOOKING_TIME_EXPIRED; }
    if (view.own_conflicts & request) { return BOOKING_STUDENT_CONFLICT; }
    for (no=0; no<COMPUTER_COUNT; ++no) {
        SeatRecommendation candidate;
        int first=start, end=start+duration;
        if ((view.masks[no] & request)!=request) { continue; }
        while (first>0 && (view.masks[no] & (1u << (first-1)))) { --first; }
        while (end<SLOT_COUNT && (view.masks[no] & (1u << end))) { ++end; }
        candidate.computer_no=no+1; candidate.run_length=end-first;
        candidate.continuity=(end-first)*10000/SLOT_COUNT;
        candidate.position=(LAB_ROWS-1-no/LAB_COLUMNS)*10000/(LAB_ROWS-1);
        candidate.balance=(SLOT_COUNT-view.loads[no])*10000/SLOT_COUNT;
        candidate.score=(candidate.continuity*weights[0]+candidate.position*weights[1]+candidate.balance*weights[2])/100;
        candidates[used++]=candidate;
    }
    qsort(candidates,used,sizeof *candidates,order);
    result.count=used<3 ? used : 3;
    for (i=0; i<result.count; ++i) { result.seats[i]=candidates[i]; }
    *out=result; return BOOKING_OK;
}
