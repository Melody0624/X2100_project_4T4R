#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "rcs_estimation.h"
#include "memory_pool.h"

static void near(float actual, float expected) {
    assert(isfinite(actual) && fabsf(actual-expected)<0.0002f);
}
int main(void) {
    /* The supplied 4.6 dBsm reference must be active immediately after boot. */
    RcsEstimate boot=rcs_calculate(138.10541727f,1.18993322f);
    assert(boot.valid && boot.calibrated);
    near(boot.dbsm,4.6f);
    rcs_clear_calibration();
    RcsEstimate a=rcs_calculate(100.0f,10.0f);
    assert(a.valid && !a.calibrated && isnan(a.dbsm));
    near(a.relative_db,140.0f);
    /* Doubling range reduces point-target received amplitude by four. */
    RcsEstimate b=rcs_calculate(87.9588002f,20.0f);
    near(b.relative_db,a.relative_db);
    assert(rcs_set_reference(10.0f,10.0f,100.0f));
    near(rcs_calculate(100.0f,10.0f).dbsm,10.0f);
    near(rcs_calculate(110.0f,10.0f).dbsm,20.0f);
    assert(rcs_calculate(100.0f,10.0f).calibrated);
    assert(!rcs_set_reference(NAN,10,100));
    assert(!rcs_set_reference(10,0,100));
    assert(!rcs_set_reference(10,10,INFINITY));
    near(rcs_calculate(100.0f,10.0f).dbsm,10.0f);
    assert(!rcs_calculate(NAN,10).valid);
    assert(!rcs_calculate(100,0).valid);
    assert(!rcs_calculate(100,-1).valid);
    assert(!rcs_calculate(100,INFINITY).valid);

    static GlbCtx ctx;
    ctx.frmInfo.frmID=17;
    ctx.detInfo.numDets=2;
    ctx.detInfo.detObj[0].pwr=100;ctx.detInfo.detObj[0].rng=10;
    ctx.detInfo.detObj[1].pwr=110;ctx.detInfo.detObj[1].rng=10;
    static GlbCtx before;
    memcpy(&before,&ctx,sizeof(ctx));
    assert(rcs_process_frame(&ctx,0)==0);
    assert(memcmp(&before,&ctx,sizeof(ctx))==0);
    const RcsFrame *frame=rcs_get_frame(0);
    assert(frame && frame->count==2 && frame->frame_id==17);
    near(frame->points[0].dbsm,10);near(frame->points[1].dbsm,20);
    /* Elevated installation stores ground range: restore propagation range. */
    ctx.radarInfo.isInHighAlt=true;ctx.radarInfo.mountingHeightSqr=36;
    ctx.detInfo.detObj[0].rng=8;
    assert(rcs_process_frame(&ctx,0)==0);
    near(rcs_get_frame(0)->points[0].dbsm,10);
    ctx.radarInfo.mountingHeightSqr=-1;
    assert(rcs_process_frame(&ctx,0)==0);
    assert(!rcs_get_frame(0)->points[0].valid);
    ctx.radarInfo.isInHighAlt=false;
    ctx.detInfo.numDets=MAX_DETECTIONS;
    for(int i=0;i<MAX_DETECTIONS;i++) {
        ctx.detInfo.detObj[i].pwr=100;ctx.detInfo.detObj[i].rng=10;
    }
    assert(rcs_process_frame(&ctx,0)==0);
    assert(rcs_get_frame(0)->count==MAX_DETECTIONS);
    near(rcs_get_frame(0)->points[MAX_DETECTIONS-1].dbsm,10);
    ctx.detInfo.numDets=0;
    assert(rcs_process_frame(&ctx,0)==0 && rcs_get_frame(0)->count==0);
    assert(!rcs_get_frame(-1) && !rcs_get_frame(MAXPOOLNUM));
    assert(rcs_process_frame(&ctx,-1)==-1);
    ctx.detInfo.numDets=MAX_DETECTIONS+1;
    assert(rcs_process_frame(&ctx,0)==-1 && rcs_get_frame(0)->count==0);
    assert(rcs_process_frame(NULL,0)==-1);
    assert(rcs_set_reference(-10,10,100));
    near(rcs_calculate(100,10).dbsm,-10);
    rcs_clear_calibration();
    assert(rcs_get_frame(0)->count==0 && !rcs_calculate(100,10).calibrated);
    puts("PASS: RCS reference calibration, R^4 compensation, invalid inputs, slant range, frame indexing and capacity");
}
