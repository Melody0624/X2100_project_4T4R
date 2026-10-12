/* Run the real retained DSP/CFAR/AoA/post sources with a portable FFT backend.
 * This tests source integration, not the MIPS SIMD implementation or RF hardware.
 * The supplier CRT object is MIPS-only and is checked by the firmware linker.
 */
#include <assert.h>
#include "rcs_estimation.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "radar_functions.h"
#include "memory_pool.h"
#include "general_functions.h"
#include "InitPhaseCalib.h"
#include "config_manager.h"
#include "board_calib_config.h"

static fft_cpx_f32 saved_matrix[8];
static float saved_angles[128];
static int matrix_saved, angles_saved;
static ParamID fail_save = PARAM_MAX_COUNT;
int uart_general_send(char *buf,int size) { fwrite(buf,1,size,stdout);return size; }
int usb_serial_gerenal_send(char *buf,int size) { return uart_general_send(buf,size); }
int param_set(ParamID id,const void *data,uint32_t size) {
    if(id==fail_save)return -1;
    if(id==PARAM_ANG_CALIB_MAT){assert(size==sizeof(saved_matrix));memcpy(saved_matrix,data,size);matrix_saved++;}
    if(id==PARAM_ANG_FFT_CFG){assert(size==sizeof(saved_angles));memcpy(saved_angles,data,size);angles_saved++;}
    return 0;
}

volatile uint8_t g_ps_cal_mode;
volatile uint8_t g_ps_cal_local_mode;

/* Only replace the unavailable hardware FFT backend and MIPS-only cache call. */
void ne10_fft_destroy_r2c_float32(ne10_fft_r2c_cfg_float32_t cfg) { free(cfg); }
static void portable_fft(fft_cpx_f32 *out, const fft_cpx_f32 *in, int n) {
    for (int i = 0; i < n; ++i) {
        unsigned int x = i, j = 0;
        for (int k = n; k > 1; k >>= 1) { j = (j << 1) | (x & 1); x >>= 1; }
        out[j] = in[i];
    }
    for (int size = 2; size <= n; size <<= 1) {
        for (int start = 0; start < n; start += size) {
            for (int k = 0; k < size / 2; ++k) {
                float angle = -2 * PI * k / size, wr = cosf(angle), wi = sinf(angle);
                fft_cpx_f32 a = out[start+k], b = out[start+k+size/2];
                float r = wr*b.r-wi*b.i, i = wr*b.i+wi*b.r;
                out[start+k] = (fft_cpx_f32){a.r+r, a.i+i};
                out[start+k+size/2] = (fft_cpx_f32){a.r-r, a.i-i};
            }
        }
    }
}
void ne10_fft_r2c_1d_float32_mxu(fft_cpx_f32 *out, float *in, ne10_fft_r2c_cfg_float32_t cfg) {
    fft_cpx_f32 input[2048], result[2048];
    assert(cfg->nfft==512 || cfg->nfft==2048);
    for (int i=0;i<cfg->nfft;++i) input[i]=(fft_cpx_f32){in[i],0};
    portable_fft(result,input,cfg->nfft);
    /* Match the existing SDK buffer convention: retain 256 complex bins. */
    memcpy(out,result,(cfg->nfft==512?256:1025)*sizeof(*out));
}
void ne10_fft_c2c_1d_float32_mxu_ai(fft_cpx_f32 *out, fft_cpx_f32 *in, ne10_fft_r2c_cfg_float32_t cfg) {
    portable_fft(out, in, cfg->nfft);
}
void velocity_disambiguation(GlbCtx *ctx, unsigned int *ranges, unsigned int count) {
    (void)ctx; (void)ranges; (void)count;
    assert(!"CRT must be disabled in portable DSP tests");
}
void save_frame_to_cache(GlbCtx *ctx, unsigned int count) { (void)ctx; (void)count; }

static void *allocate(size_t count, size_t size) {
    void *p = calloc(count, size); assert(p); return p;
}
static ne10_fft_r2c_cfg_float32_t fft_config(int n) {
    ne10_fft_r2c_cfg_float32_t cfg = allocate(1, sizeof(*cfg)); cfg->nfft = n; return cfg;
}
ne10_fft_r2c_cfg_float32_t ne10_fft_alloc_r2c_float32(ne10_int32_t n) {
    return fft_config(n);
}


static void fill_adc(GlbCtx *ctx,int rng_bin,int dop_bin,float angle) {
    uint32_t noise=12345;
    for(int rx=0;rx<4;rx++) for(int chirp=0;chirp<128;chirp++) for(int sample=0;sample<506;sample++) {
        float value=0;
        for(int tx=0;tx<2;tx++) {
            float phase = 2*PI*rng_bin*sample/512.0f + 2*PI*(dop_bin+tx*32)*chirp/128.0f;
            phase += 2*PI*ctx->basicParams.minElemSpacing[0]*(tx*4+rx)*sinf(angle*PI/180);
            value += 100*cosf(phase) * bpm_code[chirp];
        }
        noise=noise*1664525u+1013904223u;
        ctx->adcData.sf1[(rx*128+chirp)*506+sample]=value+((noise>>16)/65535.0f-.5f)*.05f;
    }
}
static void run(GlbCtx *ctx, int rng_bin, int dop_bin, float angle) {
    fill_adc(ctx,rng_bin,dop_bin,angle);
    fft_cpx_f32 calibration[8];
    for(int i=0;i<8;i++) calibration[i]=(fft_cpx_f32){1,0};
    assert(detection_processing(ctx,calibration,0)==0);
    assert(detection_processing_v2(ctx,calibration,0)==0);
    float expected=LIGHT_SPEED*26.665e6f/(2*16.699e12f*512)*rng_bin;
    DetObj *best=NULL;
    for(unsigned int i=0;i<ctx->detInfo.numDets;i++) {
        DetObj *d=&ctx->detInfo.detObj[i];
        if(!best || fabsf(d->rng-expected)<fabsf(best->rng-expected)) best=d;
    }
    assert(best);
    float speed=(dop_bin>=64?dop_bin-128:dop_bin)*ctx->waveParams.dopRes[0];
    printf("bin=%d dop=%d angle=%.1f: range=%.4f expected=%.4f velocity=%.4f angle=%.3f detections=%u\n",
           rng_bin,dop_bin,angle,best->rng,expected,best->vlc,best->azm_deg,ctx->detInfo.numDets);
    assert(fabsf(best->rng-expected)<.03f);
    assert(fabsf(best->vlc-speed)<.05f);
    assert(fabsf(best->azm_deg-angle)<.2f);
    assert(best->sfID==0 && best->rngIdx==(unsigned int)rng_bin);
    DetObj target=*best;
    ctx->detInfo.numDets=1;ctx->detInfo.detObj[0]=target;
    ctx->radarInfo.installAngComp_deg=180;
    detection_post_processing(ctx);
    assert(ctx->detInfo.numDets==1);
    assert(fabsf(ctx->detInfo.detObj[0].x_output+target.x_rcs)<.001f);
    assert(fabsf(ctx->detInfo.detObj[0].y_output+target.y_rcs)<.001f);
    assert(rcs_process_frame(ctx,0)==0);
    const RcsFrame *rcs=rcs_get_frame(0);
    assert(rcs && rcs->count==ctx->detInfo.numDets && rcs->frame_id==ctx->frmInfo.frmID);
    assert(rcs->points[0].valid && !rcs->points[0].calibrated && isnan(rcs->points[0].dbsm));
    assert(fabsf(rcs->points[0].relative_db-(target.pwr+40*log10f(target.rng)))<.001f);
    ctx->radarInfo.installAngComp_deg=0;
}
static void array_calibration(GlbCtx *ctx) {
    fft_cpx_f32 original[8];memcpy(original,angCalibMat,sizeof(original));
    InitPhaseCalibInit(ctx);
    InitPhasePara_t para={50,-15,15,15,INIT_PHASE_COMM_GADGET_SERIAL};
    SetTargetPara(&para);InitPhaseCalibProcess();
    SetInitPhaseStepFlg(INIT_PHASE_STEP_START);InitPhaseCalibProcess();
    fill_adc(ctx,10,0,0);
    assert(detection_processing(ctx,angCalibMat,0)==0);
    assert(detection_processing_v2(ctx,angCalibMat,0)==0);
    assert(GetInitPhaseStepFlg()==INIT_PHASE_STEP_ZERO_CALIB_DONE);
    InitPhaseCalibProcess();assert(matrix_saved==1);
    assert(memcmp(original,saved_matrix,sizeof(original))!=0);
    for(int i=0;i<8;i++)assert(isfinite(saved_matrix[i].r)&&isfinite(saved_matrix[i].i));
    for(int angle=-15;angle<=15;angle+=15) {
        SetTargetAngle(angle);InitPhaseCalibProcess();
        assert(GetInitPhaseStepFlg()==INIT_PHASE_STEP_FFT_CALIB_START);
        fill_adc(ctx,10,0,angle);
        assert(detection_processing(ctx,angCalibMat,0)==0);
        assert(detection_processing_v2(ctx,angCalibMat,0)==0);
        /* Moving to the next target position before its command must not
         * overwrite the sample already acknowledged for the old angle. */
        fill_adc(ctx,10,0,angle+5);
        assert(detection_processing(ctx,angCalibMat,0)==0);
        assert(detection_processing_v2(ctx,angCalibMat,0)==0);
    }
    for(int i=0;i<5;i++)InitPhaseCalibProcess();
    assert(angles_saved==1 && GetInitPhaseStepFlg()==INIT_PHASE_STEP_IDLE);
    for(int i=0;i<128;i++)assert(isfinite(saved_angles[i]));
    assert(fabsf(saved_angles[0])<.1f);
    for(int i=1;i<128;i++) {
        float k=i<64?i:i-128;
        float theory=asinf(k/(128*ctx->basicParams.minElemSpacing[0]))*180/PI;
        if(fabsf(theory)<14)assert(fabsf(saved_angles[i]-theory)<.1f);
    }
    if(ctx->basicParams.minElemSpacing[0]<0)assert(saved_angles[16]<0 && saved_angles[112]>0);
    assert(memcmp(saved_angles,ctx->algInfo.angFFTvec,sizeof(saved_angles))==0);
    /* A failed write must preserve the active parameters. */
    memcpy(original,angCalibMat,sizeof(original));
    fail_save=PARAM_ANG_CALIB_MAT;
    SetTargetPara(&para);InitPhaseCalibProcess();
    SetInitPhaseStepFlg(INIT_PHASE_STEP_START);InitPhaseCalibProcess();
    fill_adc(ctx,10,0,0);
    assert(detection_processing(ctx,angCalibMat,0)==0);
    assert(detection_processing_v2(ctx,angCalibMat,0)==0);
    InitPhaseCalibProcess();
    assert(GetInitPhaseStepFlg()==INIT_PHASE_STEP_IDLE);
    assert(memcmp(original,angCalibMat,sizeof(original))==0);
    fail_save=PARAM_MAX_COUNT;
    InitPhaseCalibProcess();
    para.AngStep=0;SetTargetPara(&para);
    assert(GetInitPhaseStepFlg()==INIT_PHASE_STEP_IDLE);
    puts("PASS: source calibration hooks, zero-phase save, all angle samples including last, signed mapping save and immediate application");
}
int main(void) {
    g_ps_cal_mode=1;
    assert(detection_processing(NULL,NULL,0)==0);
    g_ps_cal_mode=0;
    assert(detection_processing(NULL,NULL,-1)==-1);
    GlbCtx *ctx=set_parameters(false,false,0,0);
    assert(ctx && g_memoryPool[0].initialized);
    assert(winRng_sf1 && winDop_sf1 && winAzm_sf1);
    assert(fabsf(winRng_sf1[253]-1)<.001f);
    assert(fabsf(winDop_sf1[64]-1)<.001f);
    assert(fabsf(winAzm_sf1[3]-.9698463f)<.001f);
    /* The theory build must generate the real angle table at SDK startup. */
    ctx->waveParams.isCRT=false;
#ifdef EXPECT_THEORY
    float document_spacing=-.00196f/ctx->waveParams.wavelength[0];
    assert(fabsf(ctx->basicParams.minElemSpacing[0]-document_spacing)<.000001f);
    for(int i=0;i<128;i++) {
        float expected=asinf((i<64?i:i-128)/(128*document_spacing))*180/PI;
        /* Float frequency/spacing rounding is amplified near asin endpoints. */
        assert(fabsf(ctx->algInfo.angFFTvec[i]-expected)<(fabsf(expected)>80?.002f:.0001f));
        if(i<8) assert(fabsf(ctx->radarInfo.vxPosn_sf1_azm[i]-i*document_spacing)<.000001f);
    }
    puts("PASS: actual SDK angle mapping matches all 128 theoretical values");
#else
    for(int i=0;i<128;i++) ctx->algInfo.angFFTvec[i]=asinf((i<64?i:i-128)/64.0f)*180/PI;
#endif
    run(ctx,40,5,0);run(ctx,180,5,15);run(ctx,160,122,-30);
    run(ctx,180,42,30);run(ctx,180,86,-15);run(ctx,180,70,0);
    array_calibration(ctx);
    free_memory_pool(0);free(ctx);
    ctx=set_parameters(false,false,0,0);
    assert(ctx && winRng_sf1 && winDop_sf1 && winAzm_sf1);
    free_memory_pool(0);free(ctx);
    puts("PASS: real SDK initialization/reinitialization, 6 full ADC/RFFT/BPM/DFFT/DDMA/CFAR/AoA/post cases and calibration bypass");
    return 0;
}
