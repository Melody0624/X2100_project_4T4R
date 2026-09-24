#include <common.h>

struct bbt{
    uint32_t *is_bad;
    uint32_t *is_mark;
};

static struct bbt flash_bbt;

static inline int bbt_is_mark(int block)
{
    return test_bit(block, flash_bbt.is_mark);
}

static inline int bbt_is_bad(int block)
{
    return test_bit(block, flash_bbt.is_bad);
}

void bbt_mark(int block, int is_bad)
{
    if (is_bad)
        set_bit(block, flash_bbt.is_bad);
    else
        clear_bit(block, flash_bbt.is_bad);

    set_bit(block, flash_bbt.is_mark);
}
