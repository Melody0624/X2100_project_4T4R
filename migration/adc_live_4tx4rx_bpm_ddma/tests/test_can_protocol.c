#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "radar_can_protocol.h"

static void bytes(const struct rcan_frame *f, unsigned id, const uint8_t expected[8])
{
    assert(f->id==id && f->dlc==8 && f->flags==0);
    assert(memcmp(f->data,expected,8)==0);
}
struct capture { struct rcan_frame f[2002]; size_t calls, fail; };
static int capture(void *arg, enum rcan_face face, const struct rcan_frame *f)
{
    struct capture *c=arg;
    assert(face>=0 && face<=2 && c->calls<2002);
    c->f[c->calls]=*f;
    return c->calls++==c->fail ? -1 : 0;
}
int main(void)
{
    struct rcan_point p={20,-15,45,0,0,100,0x345,-50};
    struct rcan_frame f, old;
    struct rcan_command cmd;
    struct rcan_tx tx;
    static struct capture cap;
    static struct rcan_point many[2000];
    unsigned i,j;
    const uint8_t golden[8]={0x14,0x8f,0x2d,0xff,0x03,0x23,0x45,0xb2};
    assert(rcan_codec_selfcheck()==RCAN_OK);
    for(i=0;i<3;++i) {
        assert(rcan_encode_point((enum rcan_face)i,&p,&f)==0);
        bytes(&f,0x111+i,golden);
        assert(rcan_encode_boundary((enum rcan_face)i,0x123456,0,0,&f)==0);
        bytes(&f,0x103+i,(uint8_t[]){0x12,0x34,0x56,0,0,0,0,0});
        assert(rcan_encode_boundary((enum rcan_face)i,0x123456,1,2000,&f)==0);
        bytes(&f,0x106+i,(uint8_t[]){0x12,0x34,0x56,7,0xd0,0,0,0});
    }
    /* Independent unpack checks every legal range and every point ID. */
    for(i=0;i<8192;++i) {
        p.range_02m=(uint16_t)i; assert(rcan_encode_point(RCAN_FRONT,&p,&f)==0);
        assert(((unsigned)f.data[4]<<5 | f.data[5]>>3)==i);
        assert(((unsigned)(f.data[5]&7)<<8 | f.data[6])==0x345);
    }
    for(i=0;i<2048;++i) {
        p.point_id=(uint16_t)i; assert(rcan_encode_point(RCAN_FRONT,&p,&f)==0);
        assert(((unsigned)(f.data[5]&7)<<8 | f.data[6])==i);
        assert(((unsigned)f.data[4]<<5 | f.data[5]>>3)==8191);
    }
    old=f;
    p.range_02m=8192; assert(rcan_encode_point(RCAN_FRONT,&p,&f)==RCAN_RANGE);
    assert(memcmp(&f,&old,sizeof f)==0);
    p.range_02m=100; p.point_id=2048; assert(rcan_encode_point(RCAN_FRONT,&p,&f)==RCAN_RANGE);
    p.point_id=1; p.direction_2deg=128; assert(rcan_encode_point(RCAN_FRONT,&p,&f)==RCAN_RANGE);
    p.direction_2deg=45; p.rcs_half_dbsm=256; assert(rcan_encode_point(RCAN_FRONT,&p,&f)==RCAN_RANGE);
    p.rcs_half_dbsm=20; p.elevation_present=1; p.elevation_01deg=-120;
    assert(rcan_encode_point(RCAN_FRONT,&p,&f)==0 && f.data[3]==0xf8);
    p.elevation_present=0;
    for(i=0;i<251;++i) {
        int v=(int)i-125;
        p.speed_05mps=(int16_t)v; assert(rcan_encode_point(RCAN_FRONT,&p,&f)==0);
        assert((f.data[7]&127)==(v<0?-v:v)); assert(!!(f.data[7]&128)==(v<0));
    }
    p.speed_05mps=126; assert(rcan_encode_point(RCAN_FRONT,&p,&f)==RCAN_RANGE);
    p.speed_05mps=0;
    assert(rcan_encode_selftest((uint8_t[]){1,0,1},1,&f)==0);
    bytes(&f,0x101,(uint8_t[]){1,0,1,1,0,0,0,0});
    {
        struct rcan_face_status s[3]={{1,15,5,1,1,1},{2,10,6,2,2,0},{3,5,7,10,15,1}};
        assert(rcan_encode_heartbeat(s,&f)==0);
        bytes(&f,0x102,(uint8_t[]){0x6c,0xfa,0x55,0x67,0x12,0xa1,0x2f,0xa0});
        s[1].period_100ms=0; assert(rcan_encode_heartbeat(s,&f)==RCAN_RANGE);
    }
    assert(rcan_encode_update_report(1,3,&f)==0);
    bytes(&f,0x121,(uint8_t[]){1,3,0,0,0,0,0,0});
    assert(rcan_encode_update_report(0,3,&f)==RCAN_RANGE);
    f=(struct rcan_frame){RCAN_MODE,8,0,{0,1,3}};
    assert(rcan_decode_command(&f,&cmd)==0 && cmd.modes[2]==3);
    f.data[2]=4; assert(rcan_decode_command(&f,&cmd)==RCAN_RANGE);
    f=(struct rcan_frame){RCAN_ANGLE,8,0,{45,0xad,30,0x9e,15,0x8f}};
    assert(rcan_decode_command(&f,&cmd)==0 && cmd.lower[0]==-45 && cmd.upper[2]==15);
    f.data[0]=0xad; f.data[1]=45; assert(rcan_decode_command(&f,&cmd)==RCAN_RANGE);
    f=(struct rcan_frame){RCAN_DISTANCE,8,0,{100,0,80,5,50,1}};
    assert(rcan_decode_command(&f,&cmd)==0 && cmd.upper[0]==100);
    f.data[0]=200; assert(rcan_decode_command(&f,&cmd)==RCAN_UNSUPPORTED);
    f=(struct rcan_frame){RCAN_COUNT,8,0,{7,0xd0,0,16,0,0}};
    assert(rcan_decode_command(&f,&cmd)==0 && cmd.values[0]==2000);
    f.data[1]=0xd1; assert(rcan_decode_command(&f,&cmd)==RCAN_RANGE);
    f=(struct rcan_frame){RCAN_PERIOD,8,0,{0,100,1,244,3,232}};
    assert(rcan_decode_command(&f,&cmd)==0 && cmd.values[1]==500 && cmd.values[2]==1000);
    f.data[1]=99; assert(rcan_decode_command(&f,&cmd)==RCAN_RANGE);
    f=(struct rcan_frame){RCAN_POSE,8,0,{0x0e,0x10,1,0xf4,0x80,0xc8}};
    assert(rcan_decode_command(&f,&cmd)==0 && cmd.yaw_rate_01degps==-200);
    f.flags=1; assert(rcan_decode_command(&f,&cmd)==RCAN_INVALID);
    f.flags=0; f.dlc=7; assert(rcan_decode_command(&f,&cmd)==RCAN_INVALID);
    f.dlc=8; f.id=RCAN_UPDATE_DATA; assert(rcan_decode_command(&f,&cmd)==RCAN_UNSUPPORTED);
    f.id=RCAN_UPDATE_END; assert(rcan_decode_command(&f,&cmd)==RCAN_UNSUPPORTED);
    assert(rcan_decode_command(NULL,&cmd)==RCAN_INVALID);
    rcan_tx_init(&tx,NULL,NULL); assert(rcan_send_batch(&tx,RCAN_FRONT,&p,1)==RCAN_UNBOUND);
    for(i=0;i<2000;++i) { many[i]=p; many[i].point_id=(uint16_t)i; }
    cap.calls=0; cap.fail=(size_t)-1; rcan_tx_init(&tx,capture,&cap);
    tx.next_batch[RCAN_FRONT]=0xffffff;
    assert(rcan_send_batch(&tx,RCAN_FRONT,many,2000)==0 && cap.calls==2002);
    assert(tx.next_batch[RCAN_FRONT]==0 && cap.f[2001].data[3]==7 && cap.f[2001].data[4]==0xd0);
    cap.calls=0; assert(rcan_send_batch(&tx,RCAN_FRONT,NULL,0)==0 && cap.calls==2);
    assert(cap.f[0].id==0x104 && cap.f[1].id==0x107 && cap.f[1].data[4]==0);
    for(i=0;i<5;++i) {
        cap.calls=0; cap.fail=i; rcan_tx_init(&tx,capture,&cap);
        assert(rcan_send_batch(&tx,RCAN_RIGHT,many,3)==RCAN_IO && cap.calls==i+1);
        assert(tx.batches_failed==1 && tx.batches_ok==0 && tx.next_batch[RCAN_RIGHT]==1);
        for(j=0;j<i;++j) assert(cap.f[j].id!=0x108);
    }
    cap.calls=0; many[2].range_02m=8192;
    assert(rcan_send_batch(&tx,RCAN_FRONT,many,3)==RCAN_RANGE && cap.calls==0);
    puts("CAN PASS: golden frames, 8192 ranges, 2048 IDs, 251 signed speeds, controls, 2000-point batch, wrap/empty/failure; NO hardware IO");
    return 0;
}
