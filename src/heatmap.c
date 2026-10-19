#include "heatmap.h"
StatsResult heatmap_compute(const BookingContext *context, const AuthSession *session, int date, BookingHeatmap *out)
{
    WeeklyStats week;
    BookingHeatmap result={0};
    StatsResult status;
    int day,slot;
    if (!out) { return STATS_INVALID_INPUT; }
    status=stats_compute_heatmap(context,session,date,&week);
    if(status!=STATS_OK) { return status; }
    for(day=0;day<7;++day) {
        result.dates[day]=week.days[day].counts.date;
        for(slot=0;slot<SLOT_COUNT;++slot) {
            result.cells[day][slot].count=week.days[day].counts.slot_counts[slot];
            result.cells[day][slot].utilization=week.days[day].slot_utilization[slot];
        }
    }
    *out=result; return STATS_OK;
}
int heatmap_level(int count)
{
    if(count<0 || count>COMPUTER_COUNT) { return -1; }
    return count==0 ? 0 : (count-1)/8;
}
