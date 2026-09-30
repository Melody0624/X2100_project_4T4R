#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "radar_rf_profile.h"
#include "supplier_profile_options.h"

struct mock { size_t calls, fail_at; };
static int mock_row(const struct reg_line *row, void *context)
{
    struct mock *m = context;
    assert(row);
    return m->calls++ == m->fail_at ? -1 : 0;
}
static unsigned int value_at(const struct reg_line *t, size_t n, int address)
{
    size_t i;
    for (i = 0; i < n; ++i) if (t[i].addr == address) return t[i].value[0];
    assert(!"missing expected register");
    return 0;
}
int main(void)
{
    size_t n, bad, i;
    const struct reg_line *t = radar_rf_table(&n);
    struct mock m = {0, (size_t)-1};
    struct reg_line invalid[2];
    assert(n > 50 && radar_rf_table_valid());
    assert(!radar_rf_profile_ready());
    assert(radar_rf_apply_verified_profile() == -1);
    assert(value_at(t,n,0x1023) == (SUPPLIER_UART_ADC_SEND ? 0x64u : 0x2Eu));
    assert(value_at(t,n,0x1028) == (SUPPLIER_UART_ADC_SEND ? 0xF9F80080u : 0x8B680080u));
    assert(value_at(t,n,0x1029) == (SUPPLIER_USE_BPM ? 0x04052800u : 0x04010800u));
    assert(value_at(t,n,0x244) == (SUPPLIER_IS_MIRROR ? 0x000F0F00u : 0x0F0F0F0Fu));
    assert(radar_rf_execute(t,n,mock_row,&m,&bad) == 0 && m.calls == n);
    assert(bad == (size_t)-1);
    /* Inject failure at EVERY operation; nothing after it may be executed. */
    for (i = 0; i < n; ++i) {
        m.calls = 0; m.fail_at = i;
        assert(radar_rf_execute(t,n,mock_row,&m,&bad) < 0);
        assert(bad == i && m.calls == i+1);
    }
    invalid[0] = t[0]; invalid[1] = t[1];
    invalid[1].valLen = 65;
    m.calls = 0;
    assert(radar_rf_execute(invalid,2,mock_row,&m,&bad) < 0 && m.calls == 0 && bad == 1);
    invalid[1] = t[1]; invalid[1].addr = CMD_RADAR_START;
    assert(radar_rf_execute(invalid,2,mock_row,&m,&bad) < 0 && m.calls == 0);
    invalid[1] = t[1]; invalid[1].value[0] = 10001;
    assert(radar_rf_execute(invalid,2,mock_row,&m,&bad) < 0 && m.calls == 0);
    assert(radar_rf_validate(NULL,1,&bad) < 0);
    assert(radar_rf_validate(t,0,&bad) < 0);
    {
        struct reg_line flash[4];
        memset(flash, 0xff, sizeof(flash));
        flash[0] = t[0]; flash[1] = t[1];
        assert(radar_rf_flash_rows(flash, sizeof(flash), &bad) == 2);
        flash[1].valLen = 65;
        assert(radar_rf_flash_rows(flash, sizeof(flash), &bad) == 0 && bad == 1);
        memset(flash, 0xff, sizeof(flash));
        assert(radar_rf_flash_rows(flash, sizeof(flash), &bad) == 0);
        assert(radar_rf_flash_rows(flash, sizeof(flash[0]) - 1, &bad) == 0);
    }
    {
        static struct reg_line flash[128];
        assert(n < sizeof(flash) / sizeof(flash[0]));
        memcpy(flash, t, n * sizeof(*t));
        assert(radar_rf_matches_builtin(flash, n));
        /* A syntactically valid old/altered table must not drive 4TX RF. */
        assert(radar_rf_validate(flash, n - 1, &bad) == 0);
        assert(!radar_rf_matches_builtin(flash, n - 1));
        flash[0].value[0] ^= 1u;
        assert(radar_rf_validate(flash, n, &bad) == 0);
        assert(!radar_rf_matches_builtin(flash, n));
    }
    printf("RF PASS rows=%u mirror=%d bpm=%d raw_output=%d failure_cases=%u; hardware NOT tested\n",
           (unsigned)n,SUPPLIER_IS_MIRROR,SUPPLIER_USE_BPM,SUPPLIER_UART_ADC_SEND,(unsigned)n);
    return 0;
}
