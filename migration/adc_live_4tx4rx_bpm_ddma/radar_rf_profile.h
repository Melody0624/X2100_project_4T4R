#ifndef RADAR_RF_PROFILE_H
#define RADAR_RF_PROFILE_H
#include <stddef.h>
#include "cheetah/cheetah.h"
/* Callback executes one row, returning zero on success. */
typedef int (*radar_rf_row_fn)(const struct reg_line *, void *);
const struct reg_line *radar_rf_table(size_t *count);
int radar_rf_validate(const struct reg_line *table, size_t count, size_t *bad_row);
/* Returns zero for an erased, truncated, or malformed Flash register region. */
size_t radar_rf_flash_rows(const struct reg_line *table, size_t bytes, size_t *bad_row);
/* Compare parsed Flash rows with the RF profile compiled into this firmware. */
int radar_rf_matches_builtin(const struct reg_line *table, size_t count);
int radar_rf_execute(const struct reg_line *table, size_t count,
                     radar_rf_row_fn execute, void *context, size_t *bad_row);
int radar_rf_table_valid(void);
unsigned int radar_rf_table_rows(void);
int radar_rf_profile_ready(void);
int radar_rf_apply_verified_profile(void);
#endif
