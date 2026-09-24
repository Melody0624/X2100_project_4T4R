#ifndef __AFE_HT82V38_H__
#define __AFE_HT82V38_H__

/**
 * @brief 运行时设置 HT82V38 PGA 增益（需在stream_on前设置）
 * @param r 红通道PGA
 * @param g 绿通道PGA
 * @param b 蓝通道PGA
 * @return 0 成功，<0 失败
 */
int ht82v38_set_pga(unsigned short r, unsigned short g, unsigned short b);

/**
 * @brief 获取当前 HT82V38 PGA 增益
 * @param r 红通道PGA
 * @param g 绿通道PGA
 * @param b 蓝通道PGA
 * @return 0 成功，<0 失败
 */
int ht82v38_get_pga(unsigned short *r, unsigned short *g, unsigned short *b);

/**
 * @brief 运行时设置 HT82V38 OFFSET（需在stream_on前设置）
 * @param r 红通道OFFSET
 * @param g 绿通道OFFSET
 * @param b 蓝通道OFFSET
 * @return 0 成功，<0 失败
 */
int ht82v38_set_offset(unsigned short r, unsigned short g, unsigned short b);

/**
 * @brief 获取当前 HT82V38 OFFSET
 * @param r 红通道OFFSET
 * @param g 绿通道OFFSET
 * @param b 蓝通道OFFSET
 * @return 0 成功，<0 失败
 */
int ht82v38_get_offset(unsigned short *r, unsigned short *g, unsigned short *b);

#endif
