#ifndef LABSLOT_RECOMMEND_H
#define LABSLOT_RECOMMEND_H
#include "booking.h"
typedef enum { RECOMMEND_CONTINUITY, RECOMMEND_FRONT, RECOMMEND_LOAD } RecommendPreference;
typedef struct {
    int computer_no, score, continuity, position, balance, run_length;
} SeatRecommendation; /* 分数以0..10000基点表示100分，避免浮点排序误差。 */
typedef struct {
    SeatRecommendation seats[3];
    size_t count;
} Recommendation;
/* 权重百分数 C/P/L，三者和100；无随机数，不保存或创建预约。 */
const int *recommend_weights(RecommendPreference preference);
BookingResult recommend_seats(const BookingContext *context, const AuthSession *session,
                              int date, int start, int duration, RecommendPreference preference,
                              Recommendation *out);
#endif
