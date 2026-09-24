#include "radar_can_protocol.h"
#include <string.h>

int rcan_codec_selfcheck(void)
{
    const struct rcan_point p={20,-15,45,0,0,100,0x345,-50};
    const uint8_t expected[8]={0x14,0x8f,0x2d,0xff,0x03,0x23,0x45,0xb2};
    struct rcan_frame f;
    if(rcan_encode_point(RCAN_FRONT,&p,&f)!=RCAN_OK || f.id!=0x112 ||
       f.dlc!=8 || f.flags || memcmp(f.data,expected,8)) return RCAN_INVALID;
    return RCAN_OK;
}

static int face_ok(enum rcan_face face) { return face >= RCAN_LEFT && face <= RCAN_RIGHT; }
static uint8_t sm8(int v) { return (uint8_t)(v < 0 ? 0x80 | -v : v); }
static int from_sm8(uint8_t v) { return v & 0x80 ? -(int)(v & 0x7f) : v; }
static uint16_t be16(const uint8_t *p) { return (uint16_t)((uint16_t)p[0]<<8 | p[1]); }
static void init_frame(struct rcan_frame *f, uint16_t id)
{
    memset(f,0,sizeof(*f)); f->id=id; f->dlc=8;
}
int rcan_encode_point(enum rcan_face face, const struct rcan_point *p, struct rcan_frame *out)
{
    struct rcan_frame f;
    if (!p || !out || !face_ok(face)) return RCAN_INVALID;
    if (p->rcs_half_dbsm>255 || p->azimuth_deg < -45 || p->azimuth_deg>45 ||
        p->direction_2deg>127 || p->elevation_present>1 ||
        (p->elevation_present && (p->elevation_01deg < -120 || p->elevation_01deg>120)) ||
        p->range_02m>8191 || p->point_id>2047 ||
        p->speed_05mps < -125 || p->speed_05mps>125) return RCAN_RANGE;
    init_frame(&f,(uint16_t)(0x111+face));
    f.data[0]=(uint8_t)p->rcs_half_dbsm;
    f.data[1]=sm8(p->azimuth_deg);
    f.data[2]=(uint8_t)p->direction_2deg;
    f.data[3]=p->elevation_present ? sm8(p->elevation_01deg) : 0xff;
    f.data[4]=(uint8_t)(p->range_02m>>5);
    f.data[5]=(uint8_t)(((p->range_02m & 31)<<3) | (p->point_id>>8));
    f.data[6]=(uint8_t)p->point_id;
    f.data[7]=sm8(p->speed_05mps);
    *out=f; return RCAN_OK;
}
int rcan_encode_boundary(enum rcan_face face, uint32_t batch, int end,
                         uint16_t count, struct rcan_frame *out)
{
    struct rcan_frame f;
    if (!out || !face_ok(face) || (end!=0 && end!=1)) return RCAN_INVALID;
    if (batch>0xffffff || count>2000 || (!end && count)) return RCAN_RANGE;
    init_frame(&f,(uint16_t)((end ? 0x106 : 0x103)+face));
    f.data[0]=(uint8_t)(batch>>16); f.data[1]=(uint8_t)(batch>>8); f.data[2]=(uint8_t)batch;
    if (end) { f.data[3]=(uint8_t)(count>>8); f.data[4]=(uint8_t)count; }
    *out=f; return RCAN_OK;
}
int rcan_encode_selftest(const uint8_t fault[3], uint8_t model, struct rcan_frame *out)
{
    struct rcan_frame f;
    if (!fault || !out) return RCAN_INVALID;
    if (fault[0]>1 || fault[1]>1 || fault[2]>1 || model>1) return RCAN_RANGE;
    init_frame(&f,RCAN_SELFTEST);
    memcpy(f.data,fault,3); f.data[3]=model; *out=f; return RCAN_OK;
}
int rcan_encode_heartbeat(const struct rcan_face_status s[3], struct rcan_frame *out)
{
    struct rcan_frame f;
    unsigned i;
    if (!s || !out) return RCAN_INVALID;
    for(i=0;i<3;++i)
        if(s[i].mode>3 || s[i].angle_6deg>15 || s[i].distance_20m>15 ||
           s[i].period_100ms<1 || s[i].period_100ms>10 ||
           s[i].count_150<1 || s[i].count_150>15 || s[i].fault>1) return RCAN_RANGE;
    init_frame(&f,RCAN_HEARTBEAT);
    f.data[0]=(uint8_t)(s[0].mode<<6 | s[1].mode<<4 | s[2].mode<<2);
    f.data[1]=(uint8_t)(s[0].angle_6deg<<4 | s[1].angle_6deg);
    f.data[2]=(uint8_t)(s[2].angle_6deg<<4 | s[0].distance_20m);
    f.data[3]=(uint8_t)(s[1].distance_20m<<4 | s[2].distance_20m);
    f.data[4]=(uint8_t)(s[0].period_100ms<<4 | s[1].period_100ms);
    f.data[5]=(uint8_t)(s[2].period_100ms<<4 | s[0].count_150);
    f.data[6]=(uint8_t)(s[1].count_150<<4 | s[2].count_150);
    f.data[7]=(uint8_t)(s[0].fault<<7 | s[1].fault<<6 | s[2].fault<<5);
    *out=f; return RCAN_OK;
}
int rcan_encode_update_report(uint8_t failed, uint8_t reason, struct rcan_frame *out)
{
    struct rcan_frame f;
    if (!out) return RCAN_INVALID;
    if (failed>1 || reason>5 || (!failed && reason) || (failed && !reason)) return RCAN_RANGE;
    init_frame(&f,RCAN_UPDATE_REPORT); f.data[0]=failed; f.data[1]=reason;
    *out=f; return RCAN_OK;
}
int rcan_decode_command(const struct rcan_frame *f, struct rcan_command *out)
{
    struct rcan_command c;
    unsigned i;
    if (!f || !out || f->flags || f->dlc!=8 || f->id>0x7ff) return RCAN_INVALID;
    memset(&c,0,sizeof(c)); c.id=f->id;
    switch(f->id) {
    case RCAN_MODE:
        for(i=0;i<3;++i) { if(f->data[i]>3) return RCAN_RANGE; c.modes[i]=f->data[i]; }
        break;
    case RCAN_ANGLE:
    case RCAN_DISTANCE:
        for(i=0;i<3;++i) {
            /* Distance is labelled signed but range is 0..255: accept the
             * unambiguous sign-magnitude subset only until corrected. */
            if (f->id==RCAN_DISTANCE && (f->data[2*i]>127 || f->data[2*i+1]>127))
                return RCAN_UNSUPPORTED;
            c.upper[i]=(int16_t)from_sm8(f->data[2*i]);
            c.lower[i]=(int16_t)from_sm8(f->data[2*i+1]);
            if(c.upper[i]<c.lower[i]) return RCAN_RANGE;
            if(f->id==RCAN_ANGLE && (c.upper[i]>45 || c.lower[i]<-45)) return RCAN_RANGE;
        }
        break;
    case RCAN_COUNT:
    case RCAN_PERIOD:
        for(i=0;i<3;++i) {
            c.values[i]=be16(f->data+2*i);
            if(f->id==RCAN_COUNT ? c.values[i]>2000 :
                (c.values[i]<100 || c.values[i]>1000)) return RCAN_RANGE;
        }
        break;
    case RCAN_POSE: {
        uint16_t yaw=be16(f->data+4);
        c.heading_01deg=be16(f->data); c.speed_01mps=be16(f->data+2);
        c.yaw_rate_01degps=(int16_t)((yaw&0x8000) ? -(int)(yaw&0x7fff) : yaw);
        if(c.heading_01deg>3600 || c.speed_01mps>500 ||
           c.yaw_rate_01degps < -200 || c.yaw_rate_01degps>200) return RCAN_RANGE;
        break;
    }
    default: return RCAN_UNSUPPORTED; /* Includes OTA: no implicit flash writes. */
    }
    *out=c; return RCAN_OK;
}
void rcan_tx_init(struct rcan_tx *tx, rcan_send_fn send, void *context)
{
    if(!tx) return;
    memset(tx,0,sizeof(*tx)); tx->send=send; tx->context=context;
}
static int emit(struct rcan_tx *tx, enum rcan_face face, const struct rcan_frame *f)
{
    if(tx->send(tx->context,face,f)!=0) return RCAN_IO;
    ++tx->frames_accepted; return RCAN_OK;
}
int rcan_send_batch(struct rcan_tx *tx, enum rcan_face face,
                    const struct rcan_point *points, size_t count)
{
    struct rcan_frame f;
    uint32_t batch;
    size_t i;
    int ret;
    if(!tx || !face_ok(face) || (count && !points)) return RCAN_INVALID;
    if(!tx->send) return RCAN_UNBOUND;
    if(count>2000) return RCAN_RANGE;
    /* All validation happens before BEGIN, preventing malformed partial batches. */
    for(i=0;i<count;++i) {
        ret=rcan_encode_point(face,&points[i],&f); if(ret) return ret;
    }
    batch=tx->next_batch[face]&0xffffff;
    tx->next_batch[face]=(batch+1)&0xffffff; /* Never reuse a failed batch ID. */
    rcan_encode_boundary(face,batch,0,0,&f);
    if(emit(tx,face,&f)) goto failed;
    for(i=0;i<count;++i) {
        rcan_encode_point(face,&points[i],&f);
        if(emit(tx,face,&f)) goto failed;
    }
    rcan_encode_boundary(face,batch,1,(uint16_t)count,&f);
    if(emit(tx,face,&f)) goto failed;
    ++tx->batches_ok; return RCAN_OK;
failed:
    /* No forged END/count for an incomplete transmission; next BEGIN resyncs. */
    ++tx->batches_failed; return RCAN_IO;
}
