#ifndef LABSLOT_HEATMAP_H
#define LABSLOT_HEATMAP_H
#include "stats.h"
typedef struct { int count; double utilization; } HeatCell;
typedef struct { int dates[7]; HeatCell cells[7][SLOT_COUNT]; } BookingHeatmap;
StatsResult heatmap_compute(const BookingContext *context, const AuthSession *session, int date, BookingHeatmap *out);
/* 0..8,9..16,17..24,25..32,33..40；非法数量返回-1。 */
int heatmap_level(int count);
#endif
