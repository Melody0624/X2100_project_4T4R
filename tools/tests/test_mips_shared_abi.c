#include <stdint.h>
#include <stddef.h>
#include "radar_types.h"
#include "memory_pool.h"
#include "mmw_msg_pkt.h"
/* Compared in actual MIPS ELF objects for 2TX and 4TX preprocessing. */
const uint32_t shared_abi[] = {
    sizeof(GlbCtx),sizeof(DetObj),sizeof(DetObjRD),sizeof(MemoryPool),sizeof(Mmw_pkt_info),
    offsetof(GlbCtx,basicParams),offsetof(GlbCtx,waveParams),offsetof(GlbCtx,radarInfo),
    offsetof(GlbCtx,egoVehInfo),offsetof(GlbCtx,adcData),offsetof(GlbCtx,detInfo),
    offsetof(GlbCtx,detInfoRD),offsetof(GlbCtx,trkInfo),
    offsetof(MemoryPool,adcData_sf1),offsetof(MemoryPool,RDMap)
};
