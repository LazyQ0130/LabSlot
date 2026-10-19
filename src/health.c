#include "health.h"
#include <limits.h>
#include <stdlib.h>

_Static_assert(MAX_REPORTS * (HEALTH_RECENT_7_PENALTY + HEALTH_PENDING_PENALTY + HEALTH_REPEAT_PENALTY)
               + HEALTH_FAULT_PENALTY <= INT_MAX,"Health penalties must fit int");
int health_level(int score)
{
    if(score<0 || score>100) { return -1; }
    return score>=85 ? HEALTH_GOOD : score>=65 ? HEALTH_ATTENTION : score>=40 ? HEALTH_CHECK : HEALTH_HIGH;
}
const char *health_level_message(HealthLevel level)
{
    switch(level) {
    case HEALTH_GOOD:return "健康";
    case HEALTH_ATTENTION:return "关注";
    case HEALTH_CHECK:return "建议检查";
    case HEALTH_HIGH:return "高风险";
    }
    return "未知等级";
}
static int order(const void *left,const void *right)
{
    const DeviceHealth *a=left,*b=right;
    if(a->score!=b->score) { return (a->score>b->score)-(a->score<b->score); }
    return (a->computer_no>b->computer_no)-(a->computer_no<b->computer_no);
}
FaultResult health_compute(const BookingContext *context,const AuthSession *session,HealthSummary *out)
{
    HealthSummary result={0};
    Lab computers[COMPUTER_COUNT];
    FaultReportList reports={0};
    FaultResult status=fault_require_role(session,ROLE_ADMIN);
    size_t i;
    int no,today;
    if(status!=FAULT_OK) { return status; }
    if(!out) { return FAULT_INVALID_INPUT; }
    status=fault_read_time(context,&result.as_of);
    if(status!=FAULT_OK) { return status; }
    today=utils_date_serial(result.as_of.date);
    if(storage_load_computers(computers)!=1) { return FAULT_STORAGE_ERROR; }
    status=fault_from_storage(storage_load_reports(&reports));
    if(status!=FAULT_OK) { goto done; }
    for(i=0;i<reports.count;++i) {
        const FaultReport *r=&reports.items[i];
        DeviceHealth *device=&result.devices[r->computer_no-1];
        int age=today-utils_date_serial(r->date), contribution;
        if(age<0) { status=FAULT_STORAGE_ERROR; goto done; }
        if(age<7) { ++device->recent_7;contribution=HEALTH_RECENT_7_PENALTY; }
        else if(age<30) { ++device->days_8_30;contribution=HEALTH_DAYS_8_30_PENALTY; }
        else { ++device->older;contribution=HEALTH_OLDER_PENALTY; }
        if(r->status==REPORT_PENDING) { ++device->pending;contribution+=HEALTH_PENDING_PENALTY; }
        if(device->total_reports>0) { device->repeat_penalty+=HEALTH_REPEAT_PENALTY;contribution+=HEALTH_REPEAT_PENALTY; }
        ++device->total_reports;
        device->report_penalty+=contribution; /* 一条报告只累计这一次总额度。 */
    }
    for(no=0;no<COMPUTER_COUNT;++no) {
        DeviceHealth *device=&result.devices[no];
        int deduction;
        device->computer_no=no+1;device->status=computers[no].status;
        device->fault_penalty=device->status==COMPUTER_FAULT ? HEALTH_FAULT_PENALTY : 0;
        deduction=device->fault_penalty+device->report_penalty;
        device->score=deduction>=100 ? 0 : 100-deduction;
        device->level=(HealthLevel)health_level(device->score);
        if(device->status==COMPUTER_FAULT) { device->warnings|=HEALTH_KNOWN_FAULT; }
        if(device->pending>=HEALTH_PENDING_WARNING) { device->warnings|=HEALTH_MANY_PENDING; }
        if(device->recent_7+device->days_8_30>=HEALTH_REPEAT_WARNING) { device->warnings|=HEALTH_RECENT_REPEAT; }
        ++result.level_counts[device->level];
    }
    qsort(result.devices,COMPUTER_COUNT,sizeof *result.devices,order);
    *out=result;
done:
    storage_free_reports(&reports);return status;
}
