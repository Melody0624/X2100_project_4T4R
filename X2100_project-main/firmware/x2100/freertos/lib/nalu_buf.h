#ifndef __NALU_BUF_H__
#define __NALU_BUF_H__

#include <stdint.h>
#include <string.h>
#include "nalu_buf_typedef.h"

/**
 * 从 SPS中解析宽高
 * @sps: 通过函数 nalu_data_parse 获取到的 nalu_info 下的 sps信息
 * @width: 解析出的宽返回
 * @height: 解析出的高返回
*/
void nalu_sps_parse_wh(const sps_t *sps, int *width, int *height);

/**
 * 从 SPS中解析帧率
 * 返回值为解析到的fps，可能返回0，表示该SPS未携带帧率信息
 * @sps: 通过函数 nalu_data_parse 获取到的 nalu_info 下的 sps信息
*/
int nalu_sps_parse_fps(const sps_t *sps);

/**
 * 剔除防竞争字，并解析该 unit 的更多相关信息，目前仅支持解析 SPS 和 PPS类型的 nalu_unit
 * 解析成功返回0，失败返回其他值
 * @nal:存储剔除防竞争字后的相关信息
 * @data: 数据内容，即以 001或 0001开头的数据内容
 * @data_len: 数据内容长度
 * @rbsp_buf: 外部申请的存储剔除防竞争字后的数据内容
 * @rbsp_buf_len: 外部申请的 rbsp_buf长度
*/
int nalu_data_parse(nal_t *nal, const uint8_t *data, int data_len, uint8_t *rbsp_buf, int rbsp_buf_len);

/**
 * 申请、初始化解析划分nalu unit 使用的缓冲区
 * 成功返回申请到的 nalu_buf 地址，失败返回NULL
 * @size: 缓冲区大小, 后续要解析的数据将写入到该大小的空间内进行解析处理
*/
struct nalu_buf *nalu_buf_init(int size);

/**
 * 释放初始化的供解析使用缓冲区
 * @nalu_buf: 通过 nalu_buf_init 初始化申请的buf地址
*/
void nalu_buf_deinit(struct nalu_buf *nalu_buf);

/**
 * 重置 nalu_buf 解析缓冲区的状态
 * @nalu_buf: 通过 nalu_buf_init 初始化申请的buf地址
*/
void nalu_buf_clean(struct nalu_buf *nalu_buf);

/**
 * 在已经写入的数据基础上获取解析划分的 nalu unit
 * 返回值大于等于0表示成功读取缓冲区的数据大小，返回负值对应具体的错误
 * 解析划分成功返回拷贝到 buf中的数据长度，失败返回对应具体的错误码
 * @nalu_buf: 通过 nalu_buf_init 初始化申请的环形缓冲
 * @unit: 初步获取到的 nalu_unit 相关信息
 * @buf: 存放获取到的 nalu_unit 的数据内容
 * @buf_len: buf的空间大小
*/
int nalu_buf_read_unit(struct nalu_buf *nalu_buf, struct nalu_unit *unit, void *buf, int buf_len);

/**
 * 在写完所有要解析的数据，并且无法再 nalu_buf_read_unit 获取到 nalu unit时，调用该函数
 * 返回值大于等于0表示成功读取缓冲区的数据大小，返回负值对应具体的错误
 * 将缓冲区的剩余数据不必等到下一个开始码，直接作为一个 nalu unit 供使用
 * @nalu_buf: 通过 nalu_buf_init 初始化申请的buf地址
 * @unit: 初步获取到的 nalu_unit 相关信息
 * @buf: 存放获取到的 nalu_unit 的数据内容
 * @buf_len: buf的空间大小
*/
int nalu_buf_read_tail(struct nalu_buf *nalu_buf, struct nalu_unit *unit, void *buf, int buf_len);

/**
 * 尽可能地往缓冲区写入数据，以供后续的 nalu_buf_read_unit 获取解析划分的 nalu unit
 * 返回成功写入缓冲区的数据大小，返回负值对应具体的错误
 * @nalu_buf: 通过 nalu_buf_init 初始化申请的buf地址
 * @buf: 要写入缓冲区的数据起始地址
 * @size: 要写入缓冲区的数据的大小
*/
int nalu_buf_write(struct nalu_buf *nalu_buf, const void *buf, int size);
#endif /* __NALU_BUF_H__ */