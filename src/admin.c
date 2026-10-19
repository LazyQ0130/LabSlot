#include "admin.h"

#include <stdlib.h>
#include <string.h>

/* 功能：在已校验学生表中精确查找。参数：表/数量/id。返回：匹配指针或 NULL。 */
static const Student *find_student(const AccountRecords *accounts, size_t count, const char *id)
{
    size_t i;
    for (i = 0; i < count; ++i) { if (strcmp(accounts->students[i].id, id) == 0) { return &accounts->students[i]; } }
    return NULL;
}

/* 功能、参数及返回值：见 admin.h 中 admin_query_usage 的契约。 */
FaultResult admin_query_usage(const AuthSession *session, int date, int slot, AdminUsage *out)
{
    AccountRecords *accounts;
    ReservationList bookings = {0};
    AdminUsage result = {0};
    size_t count = 0, i;
    FaultResult status = fault_require_role(session, ROLE_ADMIN);
    if (status != FAULT_OK) { return status; }
    if (out == NULL || !utils_valid_date(date) || slot < 0 || slot >= SLOT_COUNT) { return FAULT_INVALID_INPUT; }
    accounts = malloc(sizeof *accounts);
    if (accounts == NULL) { return FAULT_NO_MEMORY; }
    if (!storage_load_accounts(ROLE_STUDENT, accounts, &count)) { status = FAULT_STORAGE_ERROR; goto done; }
    status = fault_from_storage(storage_load_reservations(&bookings));
    if (status != FAULT_OK) { goto done; }
    if (booking_get_seats(date, slot, &result.seats) != BOOKING_OK) { status = FAULT_STORAGE_ERROR; goto done; }
    for (i = 0; i < bookings.count; ++i) {
        const Reservation *r = &bookings.items[i];
        if (r->date == date && r->slot_index == slot && r->status == BOOKING_ACTIVE) {
            const Student *student = find_student(accounts, count, r->student_id);
            AdminUsageRow *row;
            if (student == NULL) { status = FAULT_ACCOUNT_NOT_FOUND; goto done; }
            if (result.count >= COMPUTER_COUNT) { status = FAULT_STORAGE_ERROR; goto done; }
            row = &result.rows[result.count++];
            strcpy(row->student_id, student->id); strcpy(row->name, student->name);
            row->computer_no = r->computer_no; row->reservation_id = r->id;
        }
    }
    *out = result;
done:
    storage_free_reservations(&bookings);
    utils_clear_secret(accounts, sizeof *accounts); free(accounts);
    return status;
}

/* 功能：预约按日期、时段、编号排序。参数：a/b 记录。返回：顺序。 */
static int chronological(const void *a, const void *b)
{
    const Reservation *x = a, *y = b;
    if (x->date != y->date) { return (x->date > y->date) - (x->date < y->date); }
    if (x->slot_index != y->slot_index) { return (x->slot_index > y->slot_index) - (x->slot_index < y->slot_index); }
    return (x->id > y->id) - (x->id < y->id);
}

/* 功能、参数及返回值：见 admin.h 中 admin_query_student 的契约。 */
FaultResult admin_query_student(const AuthSession *session, const char *id, BookingQuery mode, AdminStudentInfo *out)
{
    AdminStudentInfo result = {0};
    AccountRecords *accounts;
    const Student *found;
    size_t count = 0, i, used = 0;
    FaultResult status = fault_require_role(session, ROLE_ADMIN);
    if (status != FAULT_OK) { return status; }
    if (!utils_valid_id(id) || out == NULL || (mode != BOOKING_QUERY_ACTIVE && mode != BOOKING_QUERY_ALL)) { return FAULT_INVALID_INPUT; }
    accounts = malloc(sizeof *accounts);
    if (accounts == NULL) { return FAULT_NO_MEMORY; }
    if (!storage_load_accounts(ROLE_STUDENT, accounts, &count)) { status = FAULT_STORAGE_ERROR; goto done; }
    found = find_student(accounts, count, id);
    if (found == NULL) { status = FAULT_ACCOUNT_NOT_FOUND; goto done; }
    strcpy(result.student_id, found->id); strcpy(result.name, found->name);
    status = fault_from_storage(storage_load_reservations(&result.records));
    if (status != FAULT_OK) { goto done; }
    for (i = 0; i < result.records.count; ++i) {
        const Reservation *r = &result.records.items[i];
        if (strcmp(r->student_id, id) == 0) {
            ++result.history_count;
            if (r->status == BOOKING_ACTIVE) { ++result.active_count; }
            if (mode == BOOKING_QUERY_ALL || r->status == BOOKING_ACTIVE) { result.records.items[used++] = *r; }
        }
    }
    result.records.count = used;
    if (used > 1) { qsort(result.records.items, used, sizeof *result.records.items, chronological); }
    admin_free_student(out); *out = result; memset(&result, 0, sizeof result);
done:
    admin_free_student(&result);
    utils_clear_secret(accounts, sizeof *accounts); free(accounts);
    return status;
}

/* 功能、参数及返回值：见 admin.h 中 admin_free_student 的契约。 */
void admin_free_student(AdminStudentInfo *info)
{
    if (info != NULL) { storage_free_reservations(&info->records); memset(info, 0, sizeof *info); }
}
