/*
 * YAFFS: Yet Another Flash File System. A NAND-flash specific file system.
 *
 * Copyright (C) 2002-2018 Aleph One Ltd.
 *
 * Created by Charles Manning <charles@aleph1.co.uk>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/* Summaries(摘要)会将一个块中各个chunk的tags写入该块的最后1~n个chunk中
 * 后续加载时读取摘要获取所有chunk的tags比扫描所有的chunk速度会块很多
 * 
 * 存放摘要的这些数据块会被打上标签，使其看起来像是某个虚拟文件（fake file）的一部分
 * GC垃圾回收也会使用summary
 *
 */

#include "yaffs_summary.h"
#include "yaffs_packedtags2.h"
#include "yaffs_nand.h"
#include "yaffs_getblockinfo.h"
#include "yaffs_bitmap.h"
#include <driver/watchdog.h>

/*
 * summary以数组的形式进行存放
 * 数组会写入块的最后1-n个chunk,每个summary chunk开头会包含summary header
 * 只有summary header匹配时,才会认可summary中的tags内容
 */

/* Summary tags 不包含序列号(冗余) */
struct yaffs_summary_tags {
	unsigned obj_id;
	unsigned chunk_id;
	unsigned n_bytes;
};

/* Summary header */
struct yaffs_summary_header {
	unsigned version;	/* 必须和当前版本匹配 */
	unsigned block;		/* 必须在当前块 */
	unsigned seq;		/* 序列号必须一致 */
	unsigned sum;		/* 所有字节和 */
};


/* 清空summary tags */
static void yaffs_summary_clear(struct yaffs_dev *dev)
{
	if (!dev->sum_tags)
		return;
	memset(dev->sum_tags, 0, dev->chunks_per_summary *
		sizeof(struct yaffs_summary_tags));
}


void yaffs_summary_deinit(struct yaffs_dev *dev)
{
	kfree(dev->sum_tags);
	dev->sum_tags = NULL;
	kfree(dev->gc_sum_tags);
	dev->gc_sum_tags = NULL;
	dev->chunks_per_summary = 0;
}

int yaffs_summary_init(struct yaffs_dev *dev)
{
	int sum_bytes;
	int chunks_used; /* Number of chunks used by summary */
	int sum_tags_bytes;

	sum_bytes = dev->param.chunks_per_block *
			sizeof(struct yaffs_summary_tags);

	chunks_used = (sum_bytes + dev->data_bytes_per_chunk - 1)/
			(dev->data_bytes_per_chunk -
				sizeof(struct yaffs_summary_header));

	dev->chunks_per_summary = dev->param.chunks_per_block - chunks_used;
	sum_tags_bytes = sizeof(struct yaffs_summary_tags) *
				dev->chunks_per_summary;
	dev->sum_tags = kmalloc(sum_tags_bytes, GFP_NOFS);
	dev->gc_sum_tags = kmalloc(sum_tags_bytes, GFP_NOFS);
	if (!dev->sum_tags || !dev->gc_sum_tags) {
		yaffs_summary_deinit(dev);
		return YAFFS_FAIL;
	}

	yaffs_summary_clear(dev);

	return YAFFS_OK;
}

/* 计算summary内包含的chunk总数 */
static unsigned yaffs_summary_sum(struct yaffs_dev *dev)
{
	u8 *sum_buffer = (u8 *)dev->sum_tags;
	int i;
	unsigned sum = 0;

	i = sizeof(struct yaffs_summary_tags) *
				dev->chunks_per_summary;
	while (i > 0) {
		sum += *sum_buffer;
		sum_buffer++;
		i--;
	}

	return sum;
}

/* 写入整个summary到flash中 */
static int yaffs_summary_write(struct yaffs_dev *dev, int blk)
{
	struct yaffs_ext_tags tags;
	u8 *buffer;
	u8 *sum_buffer = (u8 *)dev->sum_tags;
	int n_bytes;
	int chunk_in_nand;
	int chunk_in_block;
	int result;
	int this_tx;
	struct yaffs_summary_header hdr;
	int sum_bytes_per_chunk = dev->data_bytes_per_chunk - sizeof(hdr);
	struct yaffs_block_info *bi = yaffs_get_block_info(dev, blk);

	buffer = yaffs_get_temp_buffer(dev);
	n_bytes = sizeof(struct yaffs_summary_tags) *
				dev->chunks_per_summary;
	memset(&tags, 0, sizeof(struct yaffs_ext_tags));
	tags.obj_id = YAFFS_OBJECTID_SUMMARY;
	tags.chunk_id = 1;
	tags.seq_number = dev->seq_number;
	chunk_in_block = dev->chunks_per_summary;
	chunk_in_nand = dev->alloc_block * dev->param.chunks_per_block +
						dev->chunks_per_summary;
	hdr.version = YAFFS_SUMMARY_VERSION;
	hdr.block = blk;
	hdr.seq = bi->seq_number;
	hdr.sum = yaffs_summary_sum(dev);

	do {
		this_tx = n_bytes;
		if (this_tx > sum_bytes_per_chunk)
			this_tx = sum_bytes_per_chunk;
		memcpy(buffer, &hdr, sizeof(hdr));
		memcpy(buffer + sizeof(hdr), sum_buffer, this_tx);
		tags.n_bytes = this_tx + sizeof(hdr);
		result = yaffs_wr_chunk_tags_nand(dev, chunk_in_nand,
						buffer, &tags);

		if (result != YAFFS_OK)
			break;
		yaffs_set_chunk_bit(dev, blk, chunk_in_block);
		bi->pages_in_use++;
		dev->n_free_chunks--;

		n_bytes -= this_tx;
		sum_buffer += this_tx;
		chunk_in_nand++;
		chunk_in_block++;
		tags.chunk_id++;
	} while (result == YAFFS_OK && n_bytes > 0);
	yaffs_release_temp_buffer(dev, buffer);


	if (result == YAFFS_OK)
		bi->has_summary = 1;


	return result;
}

/* 读取整个summary */
int yaffs_summary_read(struct yaffs_dev *dev,
			struct yaffs_summary_tags *st,
			int blk)
{
	struct yaffs_ext_tags tags;
	u8 *buffer;
	u8 *sum_buffer = (u8 *)st;
	int n_bytes;
	u32 chunk_id;
	int chunk_in_nand;
	int chunk_in_block;
	int result;
	int this_tx;
	struct yaffs_summary_header hdr;
	struct yaffs_block_info *bi = yaffs_get_block_info(dev, blk);
	int sum_bytes_per_chunk = dev->data_bytes_per_chunk - sizeof(hdr);

	buffer = yaffs_get_temp_buffer(dev);
	n_bytes = sizeof(struct yaffs_summary_tags) * dev->chunks_per_summary;	/* 计算有多少个summary需要读取 */
	chunk_in_block = dev->chunks_per_summary;
	chunk_in_nand = blk * dev->param.chunks_per_block +
							dev->chunks_per_summary;
	chunk_id = 1;
	do {
		this_tx = n_bytes;
		if (this_tx > sum_bytes_per_chunk)
			this_tx = sum_bytes_per_chunk;
		result = yaffs_rd_chunk_tags_nand(dev, chunk_in_nand,
						buffer, &tags);

		if (tags.chunk_id != chunk_id ||
			tags.obj_id != YAFFS_OBJECTID_SUMMARY ||
			tags.chunk_used == 0 ||
			tags.ecc_result > YAFFS_ECC_RESULT_FIXED ||
			tags.n_bytes != (this_tx + sizeof(hdr)))
				result = YAFFS_FAIL;
		if (result != YAFFS_OK)
			break;

		if (st == dev->sum_tags) {
			/* If we're scanning then update the block info */
			yaffs_set_chunk_bit(dev, blk, chunk_in_block);
			bi->pages_in_use++;
		}
		memcpy(&hdr, buffer, sizeof(hdr));
		memcpy(sum_buffer, buffer + sizeof(hdr), this_tx);
		n_bytes -= this_tx;
		sum_buffer += this_tx;
		chunk_in_nand++;
		chunk_in_block++;
		chunk_id++;
	} while (result == YAFFS_OK && n_bytes > 0);
	yaffs_release_temp_buffer(dev, buffer);

	if (result == YAFFS_OK) {
		/* Verify header */
		if (hdr.version != YAFFS_SUMMARY_VERSION ||
		    hdr.seq != bi->seq_number ||
		    hdr.sum != yaffs_summary_sum(dev))
			result = YAFFS_FAIL;
	}

	if (st == dev->sum_tags && result == YAFFS_OK)
		bi->has_summary = 1;

	return result;
}

/* 向summary中追加tags数据 */
int yaffs_summary_add(struct yaffs_dev *dev,
			struct yaffs_ext_tags *tags,
			int chunk_in_nand)
{
	struct yaffs_packed_tags2_tags_only tags_only;
	struct yaffs_summary_tags *sum_tags;
	int block_in_nand = chunk_in_nand / dev->param.chunks_per_block;
	int chunk_in_block = chunk_in_nand % dev->param.chunks_per_block;

	if (!dev->sum_tags)
		return YAFFS_OK;

	if (chunk_in_block >= 0 && chunk_in_block < dev->chunks_per_summary) {
		yaffs_pack_tags2_tags_only(dev, &tags_only, tags);
		sum_tags = &dev->sum_tags[chunk_in_block];

		sum_tags->chunk_id = tags_only.chunk_id;
		sum_tags->n_bytes = tags_only.n_bytes;
		sum_tags->obj_id = tags_only.obj_id;

		if (chunk_in_block == dev->chunks_per_summary - 1) {
			/* Time to write out the summary */
			yaffs_summary_write(dev, block_in_nand);		/* 数据chunk已经写完，写入摘要 */
			yaffs_summary_clear(dev);						/* 删除内存中的summary */
			yaffs_skip_rest_of_block(dev);
		}
	}
	return YAFFS_OK;
}

/* 查询指定的tags */
int yaffs_summary_fetch(struct yaffs_dev *dev,
			struct yaffs_ext_tags *tags,
			int chunk_in_block)
{
	struct yaffs_packed_tags2_tags_only tags_only;
	struct yaffs_summary_tags *sum_tags;
	if (chunk_in_block >= 0 && chunk_in_block < dev->chunks_per_summary) {
		sum_tags = &dev->sum_tags[chunk_in_block];
		tags_only.chunk_id = sum_tags->chunk_id;
		tags_only.n_bytes = sum_tags->n_bytes;
		tags_only.obj_id = sum_tags->obj_id;
		yaffs_unpack_tags2_tags_only(dev, tags, &tags_only);
		return YAFFS_OK;
	}
	return YAFFS_FAIL;
}

/* 垃圾回收summary*/
void yaffs_summary_gc(struct yaffs_dev *dev, int blk)
{
	struct yaffs_block_info *bi = yaffs_get_block_info(dev, blk);
	u32 i;

	if (!bi->has_summary)
		return;

	for (i = dev->chunks_per_summary;
	     i < dev->param.chunks_per_block;
	     i++) {
		if (yaffs_check_chunk_bit(dev, blk, i)) {
			yaffs_clear_chunk_bit(dev, blk, i);
			bi->pages_in_use--;
			dev->n_free_chunks++;
		}
	}
}
