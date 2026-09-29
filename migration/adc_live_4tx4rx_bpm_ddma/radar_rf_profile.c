#include "radar_rf_profile.h"

#include "supplier_profile_options.h"
#include "supplier_registers.inc"

const struct reg_line *radar_rf_table(size_t *count)
{
    if (count) *count = sizeof(supplier_registers) / sizeof(supplier_registers[0]);
    return supplier_registers;
}

int radar_rf_validate(const struct reg_line *table, size_t count, size_t *bad_row)
{
    size_t i;
    if (bad_row) *bad_row = (size_t)-1;
    if (!table || !count) return -1;
    for (i = 0; i < count; ++i) {
        const struct reg_line *row = &table[i];
        int valid = row->chipIdx == 0 && row->valLen > 0 && row->valLen <= 64;
        if (row->addr < 0) {
            valid = valid && row->valLen == 1 &&
                ((row->addr == CMD_DELAY && row->value[0] <= 10000) ||
                 (row->addr == CMD_RESET_RAMS && row->value[0] == 0));
        }
        if (!valid) {
            if (bad_row) *bad_row = i;
            return -1;
        }
    }
    return 0;
}

size_t radar_rf_flash_rows(const struct reg_line *table, size_t bytes, size_t *bad_row)
{
    size_t count = 0, capacity, i;
    const unsigned char *tail;
    if (bad_row) *bad_row = (size_t)-1;
    if (!table || bytes < 2u * sizeof(*table)) return 0;
    capacity = bytes / sizeof(*table);
    while (count < capacity &&
           !(table[count].chipIdx == 0xffffu && table[count].addr == -1))
        ++count;
    /* Require an erased terminator; never execute a full or partial table. */
    if (count == 0 || count == capacity)
        return 0;
    tail = (const unsigned char *)&table[count];
    for (i = 0; i < sizeof(*table); ++i)
        if (tail[i] != 0xffu)
            return 0;
    if (radar_rf_validate(table, count, bad_row) < 0)
        return 0;
    return count;
}

int radar_rf_execute(const struct reg_line *table, size_t count,
                     radar_rf_row_fn execute, void *context, size_t *bad_row)
{
    size_t i;
    /* Check the WHOLE table before performing the first operation. */
    if (radar_rf_validate(table, count, bad_row) < 0 || !execute) return -1;
    for (i = 0; i < count; ++i) {
        if (execute(&table[i], context) != 0) {
            if (bad_row) *bad_row = i;
            return -1;
        }
    }
    return 0;
}

int radar_rf_table_valid(void)
{
    size_t count;
    const struct reg_line *table = radar_rf_table(&count);
    return radar_rf_validate(table, count, 0) == 0;
}

unsigned int radar_rf_table_rows(void)
{
    return (unsigned int)(sizeof(supplier_registers) / sizeof(supplier_registers[0]));
}

int radar_rf_profile_ready(void)
{
    return SUPPLIER_RF_ALGORITHM_CONFIRMED && radar_rf_table_valid();
}

#if SUPPLIER_RF_ALGORITHM_CONFIRMED
#include <stdio.h>
static int hardware_row(const struct reg_line *row, void *context)
{
    /* Existing transport implements DELAY/RESET_RAMS and checks SPI errors. */
    struct reg_line copy = *row;
    (void)context;
    return set_regs_to_target(&copy, 1);
}
#endif

int radar_rf_apply_verified_profile(void)
{
#if SUPPLIER_RF_ALGORITHM_CONFIRMED
    size_t count, bad;
    const struct reg_line *table = radar_rf_table(&count);
    int ret = radar_rf_execute(table, count, hardware_row, 0, &bad);
    if (ret < 0) printf("[RF] table failure row=%u\n", (unsigned int)bad);
    return ret;
#else
    return -1; /* Imported is not RF/algorithm verified. */
#endif
}
