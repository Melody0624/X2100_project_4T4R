#ifndef _DTRNG_H_
#define _DTRNG_H_

void dtrng_init(void);    // 使能dtrng时钟、初始化控制器、并申请dtrng中断
unsigned int dtrng_read_random_data(void);    // 返回值：随机数
void dtrng_deinit(void);      // 失能dtrng时钟

#endif
