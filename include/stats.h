#ifndef SMART_LAB_STATS_H
#define SMART_LAB_STATS_H

/* P4 接口契约，P0 不实现、不调用。
 * 功能：按有效预约重算并导出每日统计至 count.txt。
 * 参数：date 为 YYYYMMDD。返回：成功 1，失败 0。
 */
int stats_export_daily(int date);

#endif
