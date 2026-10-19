#ifndef SMART_LAB_BOOKING_H
#define SMART_LAB_BOOKING_H

#include "model.h"

/* P2 接口契约，P0 不实现、不调用。
 * 功能：校验学生、日期/时段、电脑状态和冲突后持久化预约。
 * 参数：reservation 为待创建记录（id 由模块分配）。返回：成功 1，失败 0。
 */
int booking_create(Reservation *reservation);

#endif
