#ifndef __HELIX_OPS_H__
#define __HELIX_OPS_H__

extern int ingenic_helix_init(void);
extern int ingenic_helix_deinit(void);
extern void *ingenic_helix_ctx_init(unsigned int fmt, int codec_type);
extern int ingenic_helix_ctx_deinit(void *ctx);

void ingenic_vcodec_helix_work_thread(void *data);
int ingenic_vcodec_helix_set_param(void *data, unsigned int id, int value);
int ingenic_vcodec_helix_get_param(void *data, unsigned int id, int *value);
int ingenic_vcodec_helix_set_fmt_out(void *data, int width, int height,unsigned int format);
int ingenic_vcodec_helix_set_fmt_cap(void *data, int width, int height,unsigned int format);
int ingenic_vcodec_helix_start(void *data);
int ingenic_vcodec_helix_stop(void *data);
int ingenic_vcodec_helix_create_srcbuf(void *data, int create_num);
int ingenic_vcodec_helix_destroy_srcbuf(void *data);
unsigned int ingenic_vcodec_helix_get_srcbuf_np(void *data);
int ingenic_vcodec_helix_set_srcbuf(void *data, int index, unsigned int vaddr, unsigned int paddr, unsigned int size);
int ingenic_vcodec_helix_get_srcbuf(void *data, int index, int plane, unsigned int *vaddr, unsigned int *size);
int ingenic_vcodec_helix_create_destbuf(void *data, int create_num);
int ingenic_vcodec_helix_destroy_destbuf(void *data);
unsigned int ingenic_vcodec_helix_get_destbuf_np(void *data);
int ingenic_vcodec_helix_set_destbuf(void *data, int index, unsigned int vaddr, unsigned int paddr, unsigned int size);
int ingenic_vcodec_helix_get_destbuf(void *data, int index, unsigned int *vaddr, unsigned int *size);
int ingenic_vcodec_helix_srcbuf_wait(void *data, int block);
int ingenic_vcodec_helix_srcbuf_queue(void *data, int index);
int ingenic_vcodec_helix_srcbuf_dequeue(void *data, int *index);
int ingenic_vcodec_helix_destbuf_wait(void *data, int block);
int ingenic_vcodec_helix_destbuf_queue(void *data, int index);
int ingenic_vcodec_helix_destbuf_dequeue(void *data, int *index, unsigned int *vaddr, unsigned int *size);

#endif
