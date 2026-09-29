#include <assert.h>
#include <stddef.h>
#include "../supplier_tracking_adapter.c"

int main(void)
{
    /* track_output in the supplied MIPS archive reads these exact offsets. */
    if (sizeof(void *) == 4u) {
        assert(offsetof(GlbCtx, radarInfo.isCompInstallAng) == 92u);
        assert(offsetof(GlbCtx, radarInfo.installAngComp_deg) == 96u);
    }
    radar_tracking_reset();
    assert(supplier_ctx != NULL);
    assert(supplier_ctx->radarInfo.isCompInstallAng);
    assert(supplier_ctx->radarInfo.installAngComp_deg == 180.0f);
    supplier_ctx->radarInfo.isCompInstallAng = false;
    radar_tracking_reset();
    assert(supplier_ctx->radarInfo.isCompInstallAng);
    puts("SUPPLIER_MOUNTING=PASS archive flag/angle ABI and reset; no archive execution on host");
    return 0;
}
