/* Exercise the real config manager against a bounded in-memory NOR device. */
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <driver/sfc_nor.h>
static uint8_t flash[0x25000];
static uint8_t heap[16384] __attribute__((aligned(64)));
static unsigned int erases;
static void *mock_malloc(size_t n) { assert(n<=sizeof(heap));return heap; }
static void mock_free(void *p) { assert(p==heap); }
static uint32_t offset(uint32_t addr,uint32_t n) {
    assert(addr>=0x1DB000 && (uint64_t)addr+n<=0x200000);
    return addr-0x1DB000;
}
int sfc_nor_flash_read(uint32_t addr,uint32_t n,uint8_t *p) { memcpy(p,flash+offset(addr,n),n);return n; }
int sfc_nor_flash_write(uint32_t addr,uint32_t n,const uint8_t *p) { memcpy(flash+offset(addr,n),p,n);return n; }
int sfc_nor_flash_erase(uint32_t addr,uint32_t n) { memset(flash+offset(addr,n),255,n);erases++;return 0; }
const struct storage_info *sfc_nor_flash_info(void) { static struct storage_info i={4096};return &i; }
int get_nor_partition_information_by_name(char *name,uint32_t *a,uint32_t *n) {
    assert(!strcmp(name,"config"));*a=0x1DB000;*n=0x25000;return 0;
}
#define malloc mock_malloc
#define free mock_free
#include "../../X2100_project-main/firmware/x2100/freertos/vendor/config_manager.c"
#undef malloc
#undef free
int main(void) {
    assert((uintptr_t)heap<=UINT32_MAX); /* SDK uses 32-bit DMA addresses. */
    memset(flash,255,sizeof(flash));
#ifdef EXPECT_FOUR_TX
    assert(sizeof(((SysConfigLayout*)0)->angCalibMat)==32*sizeof(float));
    assert(CONFIG_MAGIC_WORD!=0x52414456);
    assert(g_param_table[PARAM_ANG_CALIB_MAT].length==128);
#else
    assert(CONFIG_MAGIC_WORD==0x52414456);
    assert(sizeof(((SysConfigLayout*)0)->angCalibMat)==64);
#endif
    assert(sizeof(SysConfigLayout)<=sizeof(flash));
    assert(config_manager_init(NULL,0,0,0,0,0)==0);
    uint32_t magic;memcpy(&magic,flash,4);assert(magic==CONFIG_MAGIC_WORD);
    float matrix[2*NUM_VX_SF1],got[2*NUM_VX_SF1],angles[128],angle_copy[128];
    for(int i=0;i<2*NUM_VX_SF1;i++)matrix[i]=i*.25f;
    for(int i=0;i<128;i++)angles[i]=(float)i;
    int32_t frames=32,frames_copy=0;
    assert(param_set(PARAM_ANG_FFT_CFG,angles,sizeof(angles))==0);
    assert(param_set(PARAM_MAX_FRAME_CNT,&frames,sizeof(frames))==0);
    assert(param_set(PARAM_ANG_CALIB_MAT,matrix,sizeof(matrix))==0);
    assert(param_get(PARAM_ANG_CALIB_MAT,got,sizeof(got))==0 && !memcmp(matrix,got,sizeof(matrix)));
    assert(param_get(PARAM_ANG_FFT_CFG,angle_copy,sizeof(angle_copy))==0 && !memcmp(angles,angle_copy,sizeof(angles)));
    assert(param_get(PARAM_MAX_FRAME_CNT,&frames_copy,sizeof(frames_copy))==0 && frames_copy==frames);
    unsigned int before=erases;assert(config_manager_init(NULL,0,0,0,0,0)==0);assert(before==erases);
#ifdef EXPECT_FOUR_TX
    float short_matrix[16]={0};
    assert(param_set(PARAM_ANG_CALIB_MAT,short_matrix,sizeof(short_matrix))==-1);
    assert(param_get(PARAM_ANG_CALIB_MAT,short_matrix,sizeof(short_matrix))==-1);
    magic=0x52414456;memcpy(flash,&magic,4);
    assert(config_manager_init(NULL,0,0,0,0,0)==0);
    memcpy(&magic,flash,4);assert(magic==CONFIG_MAGIC_WORD && erases>before);
#endif
    puts("PASS: actual config layout/magic, full matrix roundtrip, adjacent fields and legacy-layout rejection");
}
