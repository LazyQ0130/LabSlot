#ifndef SMART_LAB_FAULT_H
#define SMART_LAB_FAULT_H

#include "model.h"

/* P3 接口契约，P0 不实现、不调用。
 * 功能：校验并保存学生故障报告，不直接更改电脑状态。
 * 参数：report 为待提交报告（id 由模块分配）。返回：成功 1，失败 0。
 */
int fault_submit(FaultReport *report);

#endif
