#ifndef __FELIX_OPS_H__
#define __FELIX_OPS_H__

enum felix_param_type {
	FELIX_FORMAT = 0,
	FELIX_WIDTH,
	FELIX_HEIGHT,
};

int ingenic_vcodec_felix_get_param(void *data, unsigned int id, int *value);


extern int ingenic_felix_init(void);
extern int ingenic_felix_deinit(void);
extern void *ingenic_felix_ctx_init(void);
extern int ingenic_felix_ctx_deinit(void *ctx);

void ingenic_vcodec_felix_work_thread(void *data);

int ingenic_vcodec_felix_set_param(void *data, int width, int height, int format);
int ingenic_vcodec_felix_get_param(void *data, unsigned int id, int *value);

int ingenic_vcodec_felix_start(void *data);
int ingenic_vcodec_felix_stop(void *data);

int ingenic_vcodec_felix_create_srcbuf(void *data, int create_num);
int ingenic_vcodec_felix_set_srcbuf(void *data, int index, unsigned int vaddr, unsigned int paddr);
int ingenic_vcodec_felix_get_srcbuf(void *data, int index, unsigned int *vaddr, unsigned int *size);

int ingenic_vcodec_felix_create_dstbuf(void *data, int create_num);
int ingenic_vcodec_felix_set_dstbuf(void *data, int index, unsigned int vaddr, unsigned int paddr, unsigned int size);
int ingenic_vcodec_felix_get_dstbuf(void *data, int np, int index, unsigned int *vaddr, unsigned int *size);

int ingenic_vcodec_felix_srcbuf_wait(void *data, int block);
int ingenic_vcodec_felix_srcbuf_queue(void *data, int index);
int ingenic_vcodec_felix_srcbuf_dequeue(void *data, int *index);

int ingenic_vcodec_felix_dstbuf_wait(void *data, int block);
int ingenic_vcodec_felix_dstbuf_queue(void *data, int index);
int ingenic_vcodec_felix_dstbuf_dequeue(void *data, int num, int *index, unsigned int *vaddr, unsigned int *size);

#endif
