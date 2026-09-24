/*
 * YAFFS: Yet another Flash File System . A NAND-flash specific file system.
 *
 * Copyright (C) 2002-2018 Aleph One Ltd.
 *
 * Created by Charles Manning <charles@aleph1.co.uk>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1 as
 * published by the Free Software Foundation.
 *
 * Note: Only YAFFS headers are LGPL, YAFFS C code is covered by GPL.
 */

#ifndef __YAFFS_GUTS_H__
#define __YAFFS_GUTS_H__

#include <sys/types.h>
#include "yportenv.h"

typedef signed long      off_t;
// typedef int              mode_t;


#define YAFFS_OK	1
#define YAFFS_FAIL  0

/* Give us a  Y=0x59,
 * Give us an A=0x41,
 * Give us an FF=0xff
 * Give us an S=0x53
 * And what have we got...
 */
#define YAFFS_MAGIC			0x5941ff53

/*
 * Tnodes form a tree with the tnodes in "levels"
 * Levels greater than 0 hold 8 slots which point to other tnodes.
 * Those at level 0 hold 16 slots which point to chunks in NAND.
 *
 * A maximum level of 8 thust supports files of size up to:
 *
 * 2^(3*MAX_LEVEL+4)
 *
 * Thus a max level of 8 supports files with up to 2^^28 chunks which gives
 * a maximum file size of around 512Gbytees with 2k chunks.
 */
#define YAFFS_NTNODES_LEVEL0		16
#define YAFFS_TNODES_LEVEL0_BITS	4
#define YAFFS_TNODES_LEVEL0_MASK	0xf

#define YAFFS_NTNODES_INTERNAL		(YAFFS_NTNODES_LEVEL0 / 2)
#define YAFFS_TNODES_INTERNAL_BITS	(YAFFS_TNODES_LEVEL0_BITS - 1)
#define YAFFS_TNODES_INTERNAL_MASK	0x7
#define YAFFS_TNODES_MAX_LEVEL		8
#define YAFFS_TNODES_MAX_BITS		(YAFFS_TNODES_LEVEL0_BITS + \
					YAFFS_TNODES_INTERNAL_BITS * \
					YAFFS_TNODES_MAX_LEVEL)
#define YAFFS_MAX_CHUNK_ID		((1 << YAFFS_TNODES_MAX_BITS) - 1)

#define YAFFS_MAX_FILE_SIZE_32		0x7fffffff

/* Constants for YAFFS1 mode */
#define YAFFS_BYTES_PER_SPARE		16
#define YAFFS_BYTES_PER_CHUNK		512
#define YAFFS_CHUNK_SIZE_SHIFT		9
#define YAFFS_CHUNKS_PER_BLOCK		32
#define YAFFS_BYTES_PER_BLOCK	(YAFFS_CHUNKS_PER_BLOCK*YAFFS_BYTES_PER_CHUNK)

#define YAFFS_MIN_YAFFS2_CHUNK_SIZE	1024
#define YAFFS_MIN_YAFFS2_SPARE_SIZE	32



#define YAFFS_ALLOCATION_NOBJECTS	100
#define YAFFS_ALLOCATION_NTNODES	100
#define YAFFS_ALLOCATION_NLINKS		100

#define YAFFS_NOBJECT_BUCKETS		256

#define YAFFS_OBJECT_SPACE		0x40000
#define YAFFS_MAX_OBJECT_ID		(YAFFS_OBJECT_SPACE - 1)

/* Binary data version stamps */
#define YAFFS_SUMMARY_VERSION		1

#ifdef CONFIG_YAFFS_UNICODE
#define YAFFS_MAX_NAME_LENGTH		127
#define YAFFS_MAX_ALIAS_LENGTH		79
#else
#define YAFFS_MAX_NAME_LENGTH		255
#define YAFFS_MAX_ALIAS_LENGTH		159
#endif

#define YAFFS_SHORT_NAME_LENGTH		15

/* Some special object ids for pseudo objects */
#define YAFFS_OBJECTID_ROOT		1
#define YAFFS_OBJECTID_LOSTNFOUND	2
#define YAFFS_OBJECTID_UNLINKED		3
#define YAFFS_OBJECTID_DELETED		4

/* Fake object Id for summary data */
#define YAFFS_OBJECTID_SUMMARY		0x10

/* Pseudo object ids for checkpointing */
#define YAFFS_OBJECTID_CHECKPOINT_DATA	0x20
#define YAFFS_SEQUENCE_CHECKPOINT_DATA	0x21

#define YAFFS_MAX_SHORT_OP_CACHES	20

#define YAFFS_N_TEMP_BUFFERS		6

/* We limit the number attempts at sucessfully saving a chunk of data.
 * Small-page devices have 32 pages per block; large-page devices have 64.
 * Default to something in the order of 5 to 10 blocks worth of chunks.
 * 单次数据块写入的最大重试次数阈值
 */
#define YAFFS_WR_ATTEMPTS		(5*64)

/* Sequence numbers are used in YAFFS2 to determine block allocation order.
 * The range is limited slightly to help distinguish bad numbers from good.
 * This also allows us to perhaps in the future use special numbers for
 * special purposes.
 * EFFFFF00 allows the allocation of 8 blocks/second (~1Mbytes) for 15 years,
 * and is a larger number than the lifetime of a 2GB device.
 * 分配的序列号范围(序列号用于标记文件时间, 越小文件越老)
 */
#define YAFFS_LOWEST_SEQUENCE_NUMBER	0x00001000
#define YAFFS_HIGHEST_SEQUENCE_NUMBER	0xefffff00

/* Special sequence number for bad block that failed to be marked bad */
#define YAFFS_SEQUENCE_BAD_BLOCK	0xffff0000

/* ChunkCache is used for short read/write operations.*/
struct yaffs_cache {
	struct yaffs_obj *object;
	int chunk_id;
	int last_use;
	int dirty;
	int n_bytes;		/* Only valid if the cache is dirty */
	int locked;		/* Can't push out or flush while locked. */
	u8 *data;
};

/* yaffs1 tags structures in RAM
 * NB This uses bitfield. Bitfields should not straddle a u32 boundary
 * otherwise the structure size will get blown out.
 */

struct yaffs_tags {
	u32 chunk_id:20;
	u32 serial_number:2;
	u32 n_bytes_lsb:10;
	u32 obj_id:18;
	u32 ecc:12;
	u32 n_bytes_msb:2;
};

union yaffs_tags_union {
	struct yaffs_tags as_tags;
	u8  as_bytes[8];
	u32 as_u32[2];
};


/* Stuff used for extended tags in YAFFS2 */

enum yaffs_ecc_result {
	YAFFS_ECC_RESULT_UNKNOWN,
	YAFFS_ECC_RESULT_NO_ERROR,
	YAFFS_ECC_RESULT_FIXED,
	YAFFS_ECC_RESULT_UNFIXED
};

/*
 * Object type enum:
 * When this is stored in flash we store it as a u32 instead
 * to prevent any alignment change issues as compiler variants change.
 */

enum yaffs_obj_type {
	YAFFS_OBJECT_TYPE_UNKNOWN,
	YAFFS_OBJECT_TYPE_FILE,
	YAFFS_OBJECT_TYPE_SYMLINK,
	YAFFS_OBJECT_TYPE_DIRECTORY,
	YAFFS_OBJECT_TYPE_HARDLINK,
	YAFFS_OBJECT_TYPE_SPECIAL
};

#define YAFFS_OBJECT_TYPE_MAX YAFFS_OBJECT_TYPE_SPECIAL

/* chunk元数据结构体(完整元数据) */
struct yaffs_ext_tags {
	unsigned chunk_used;	/*  chunk状态:是否被使用 */
	unsigned obj_id;	/* 0:未被使用 */
	unsigned chunk_id;	/* 0:chunk存储头部信息 其他:chunk存储数据信息 */
	unsigned n_bytes;	/* 数据chunk表示这个chunk存储的数据大小， obj header chunk 表示整个文件存储的数据大小 */

	/* 仅在读取chunk时生效 */
	enum yaffs_ecc_result ecc_result;	/* ECC校验结果 */
	unsigned block_bad;					/* 是否为坏块 */

	/* YAFFS 1 stuff */
	unsigned is_deleted;	/* The chunk is marked deleted */
	unsigned serial_number;	/* Yaffs1 2-bit serial number */

	/* YAFFS2 stuff */
	unsigned seq_number;	/* 块序列号:用于进行块排序和标记数据新旧 */

	/* 额外信息, 仅chunk存储object header有效 (YAFFS2 only) */
	unsigned extra_available;	/* 非0:表示以下扩展数据有效 */
	unsigned extra_parent_id;	/* 双亲object */
	unsigned extra_is_shrink;	/* 是否为被截断的文件 */
	unsigned extra_shadows;		/* 是否是影子副本 */

	enum yaffs_obj_type extra_obj_type;	/* obj类型 */

	loff_t extra_file_size;		/* 如果是文件,对应文件长度 */
	unsigned extra_equiv_id;	/* 硬链接的原始obj id */
};

/* Spare structure for YAFFS1 */
struct yaffs_spare {
	u8 tb0;
	u8 tb1;
	u8 tb2;
	u8 tb3;
	u8 page_status;		/* set to 0 to delete the chunk */
	u8 block_status;
	u8 tb4;
	u8 tb5;
	u8 ecc1[3];
	u8 tb6;
	u8 tb7;
	u8 ecc2[3];
};

/*Special structure for passing through to mtd */
struct yaffs_nand_spare {
	struct yaffs_spare spare;
	int eccres1;
	int eccres2;
};

/* Block data in RAM */

enum yaffs_block_state {
	YAFFS_BLOCK_STATE_UNKNOWN = 0,	/* 状态未知 */

	YAFFS_BLOCK_STATE_SCANNING,		/* 正在被扫描 */

	YAFFS_BLOCK_STATE_NEEDS_SCAN,
	/*
	 * 块状态可能为空\满\分配中,需要仔细扫描确认状态
	 * 对于yaffs2,这种状态的块应该已经被分配seq_number
	 */

	YAFFS_BLOCK_STATE_EMPTY,
	/* This block is empty */

	YAFFS_BLOCK_STATE_ALLOCATING,
	/*
	 * 块内至少有一个page被分配(存储了有效数据),则被标记为这种状态
	 * 同一时间只能有一个这种状态的块,如果挂载时发现有部分页被分配的块,则会被标记为FULL
	 */

	YAFFS_BLOCK_STATE_FULL,
	/*
	 * 块是满的
	 * 挂载时部分分配的块(非满但有page存储了有效数据)依旧会被标记为满块
	 */

	YAFFS_BLOCK_STATE_DIRTY,
	/*
	 * 块曾经是满的，但现在所有chunk都已被删除
	 * 需要被擦除后重新使用
	 * 擦除后会变为EMPTY状态
	 */

	YAFFS_BLOCK_STATE_CHECKPOINT,
	/* 检查点数据块,专门存储检查点信息 */

	YAFFS_BLOCK_STATE_COLLECTING,
	/* 正在被GC处理的块,会将块内数据前移到其他空块 */

	YAFFS_BLOCK_STATE_DEAD
	/* 坏块,不再使用 */
};

#define	YAFFS_NUMBER_OF_BLOCK_STATES (YAFFS_BLOCK_STATE_DEAD + 1)

/* 块信息结构体 */
struct yaffs_block_info {

	s32 soft_del_pages:10;	/* 被软删除的chunk数量 */
	s32 pages_in_use:10;	/* 被使用的chunk数量 */
	u32 block_state:4;		/* 块状态, 存储yaffs_block_state枚举值 */
				/* NB use unsigned because enum is sometimes
				 * an int */
	u32 needs_retiring:1;	/* 数据失效,是否需要将块设置为退休 */
				/*need to get valid data off and retire*/
	u32 skip_erased_check:1;/* 是否跳过擦除校验 */
	u32 gc_prioritise:1;	/* ECC校验或其他错误,将block抓到GC回收 */
	u32 chunk_error_strikes:3;	/* 块出错次数 */
	u32 has_summary:1;	/* 是否存在summary,用于快速挂载 */

	u32 has_shrink_hdr:1;	/* This block has at least one shrink header */
	u32 seq_number;		/* 块序列号 for yaffs2 */

};

union yaffs_block_info_union {
	struct yaffs_block_info bi;
	u32	as_u32[2];
};

/* -------------------------- Object structure -------------------------------*/
/* obj数据在NAND中的存储形式 */

struct yaffs_obj_hdr {
	u32 type;  /* obj类型  */

	/* Apply to everything  */
	u32 parent_obj_id;		/* 双亲obj */
	u16 sum_no_longer_used;	/* checksum of name. No longer used */
	YCHAR name[YAFFS_MAX_NAME_LENGTH + 1];	/* 文件名称 */

	/* 文件权限与时间戳（适用于非硬链接对象） */
	u32 yst_mode;		/* protection 文件权限和类型 */

	u32 yst_uid;		/* 用户ID */
	u32 yst_gid;		/* 组ID */
	u32 yst_atime;		/* 最后访问时间 */
	u32 yst_mtime;		/* 最后修改时间 */
	u32 yst_ctime;		/* 最后状态变更时间 */

	/* 文件大小 低32位 */
	u32 file_size_low;

	/* 仅适用于硬链接,指向的原始文件obj id */
	int equiv_id;

	/* 仅适用于符号链接,链接目标路径 */
	YCHAR alias[YAFFS_MAX_ALIAS_LENGTH + 1];

	u32 yst_rdev;	/* 设备号 */

	u32 win_ctime[2];	/*windows时间戳*/
	u32 win_atime[2];	/*windows时间戳*/
	u32 win_mtime[2];	/*windows时间戳*/

	u32 inband_shadowed_obj_id;
	u32 inband_is_shrink;

	u32 file_size_high;	/* 文件大小 高32位 */
	u32 reserved[1];
	int shadows_obj;	/* 影子obj id */

	/* is_shrink applies to object headers written when wemake a hole. */
	u32 is_shrink;		/* 是否发生截断 */

};

/*--------------------------- Tnode -------------------------- */
/* Tnode内部指针结构，用于构建Tnode树 */
struct yaffs_tnode {
	struct yaffs_tnode *internal[YAFFS_NTNODES_INTERNAL];
};

/*------------------------  Object -----------------------------*/
/* An object can be one of:
 * - a directory (no data, has children links
 * - a regular file (data.... not prunes :->).
 * - a symlink [symbolic link] (the alias).
 * - a hard link
 */

/* The file variant has three file sizes:
 *  - file_size : size of file as written into Yaffs - including data in cache.
 *  - stored_size - size of file as stored on media.
 *  - shrink_size - size of file that has been shrunk back to.
 *
 * The stored_size and file_size might be different because the data written
 * into the cache will increase the file_size but the stored_size will only
 * change when the data is actually stored.
 *
 */
/* object可以表示的类型结构体 */
struct yaffs_file_var {
	loff_t file_size;
	loff_t stored_size;
	loff_t shrink_size;
	int top_level;
	struct yaffs_tnode *top;
};

struct yaffs_dir_var {
	struct list_head children;	/* list of child links */
	struct list_head dirty;	/* Entry for list of dirty directories */
};

struct yaffs_symlink_var {
	YCHAR *alias;
};

struct yaffs_hardlink_var {
	struct yaffs_obj *equiv_obj;
	u32 equiv_id;
};

union yaffs_obj_var {
	struct yaffs_file_var file_variant;
	struct yaffs_dir_var dir_variant;
	struct yaffs_symlink_var symlink_variant;
	struct yaffs_hardlink_var hardlink_variant;
};

struct yaffs_obj {
	u8 deleted:1;		/* 已删除标记 */
	u8 soft_del:1;		/* 软删除标记(数据无效,但flash中仍保存数据) */
	u8 unlinked:1;		/* 已删除链接标记 */
	u8 fake:1;		/* 伪对象标记(数据没有存储在flash中,仅在内存中存在) */
	u8 rename_allowed:1;	/* 是否可以重命名 */
	u8 unlink_allowed:1;	/* 是否可以删除链接 */
	u8 dirty:1;		/* 脏标记(数据出现更新,需要写回flash) */
	u8 valid:1;		/* 有效性标记(当先扫到数据再扫到obj chunk的文件,需要验证其是否有效) */
	u8 lazy_loaded:1;	/* 延迟加载标记(元数据未加载完全,后续加载) */

	u8 defered_free:1;	/* 延迟释放标记(flash中已删除,但内存中未被释放) */
	u8 being_created:1;	/* 创建中标记, 用于跳过某些验证 */
	u8 is_shadowed:1;	/* 影子化标记 */

	u8 xattr_known:1;	/* obj是否已知xattr */
	u8 has_xattr:1;		/* obj是否存在扩展属性 */

	u8 serial;		/* chunk序列号 */
	u16 sum;		/* sum of the name to speed searching */

	struct yaffs_dev *my_dev;	/* The device I'm on */

	struct list_head hash_link;	/* 哈希链表 */

	struct list_head hard_links;	/* 硬链接链表 */

	/* directory structure stuff */
	/* also used for linking up the free list */
	struct yaffs_obj *parent;
	struct list_head siblings;

	/* flash中obj header所在的chunk号 */
	int hdr_chunk;

	int n_data_chunks;	/* 文件数据chunk数量 */

	u32 obj_id;		/* the object id value */

	u32 yst_mode;

	YCHAR short_name[YAFFS_SHORT_NAME_LENGTH + 1];

#ifdef CONFIG_YAFFS_WINCE
	u32 win_ctime[2];
	u32 win_mtime[2];
	u32 win_atime[2];
#else
	u32 yst_uid;
	u32 yst_gid;
	u32 yst_atime;
	u32 yst_mtime;
	u32 yst_ctime;
#endif

	u32 yst_rdev;

	void *my_inode;

	u32 variant_type; /* enum yaffs_object_type */

	union yaffs_obj_var variant;

};

struct yaffs_obj_bucket {
	struct list_head list;
	int count;
};


/*--------------------- Temporary buffers ----------------
 *
 * These are chunk-sized working buffers. Each device has a few.
 */

struct yaffs_buffer {
	u8 *buffer;
	int in_use;
};

/*----------------- Device ---------------------------------*/

struct yaffs_param {
	const YCHAR *name;

	/*
	 * Entry parameters set up way early. Yaffs sets up the rest.
	 * The structure should be zeroed out before use so that unused
	 * and default values are zero.
	 */

	int inband_tags;	/* Use unband tags */
	u32 total_bytes_per_chunk;	/* chunk大小 */
	u32 chunks_per_block;	/* 块内chunk数量 */
	u32 spare_bytes_per_chunk;	/* oob区域大小 */
	u32 start_block;	/* 起始块 */
	u32 end_block;		/* 结尾块 */
	u32 n_reserved_blocks;	/* 预留块 */

	u32 n_caches;		/* 预留缓存 */
	int cache_bypass_aligned; /* 对齐数据跳过缓存 */

	int use_nand_ecc;	/* 是否使用nand ecc校验 (yaffs1) */
	int tags_9bytes;	/* Use 9 byte tags */
	int no_tags_ecc;	/* 对于tags元数据是否使用ecc校验 (yaffs2) */

	int is_yaffs2;		/* Use yaffs2 mode on this device */

	int empty_lost_n_found;	/* Auto-empty lost+found directory on mount */

	int refresh_period;	/* 块刷新周期 */

	/* Checkpoint 相关配置 */
	u8 skip_checkpt_rd;
	u8 skip_checkpt_wr;

	int enable_xattr;	/* Enable xattribs */

	int max_objects;	/* 最大obj数量 */

	int hide_lost_n_found;  /* Set non-zero to hide the lost-n-found dir. */

	int stored_endian; /* 0=cpu endian, 1=little endian, 2=big endian */

	/* The remove_obj_fn function must be supplied by OS flavours that
	 * need it.
	 * yaffs direct uses it to implement the faster readdir.
	 * Linux uses it to protect the directory during unlocking.
	 */
	void (*remove_obj_fn) (struct yaffs_obj *obj);

	/* Callback to mark the superblock dirty */
	void (*sb_dirty_fn) (struct yaffs_dev *dev);

	/*  Callback to control garbage collection. */
	unsigned (*gc_control_fn) (struct yaffs_dev *dev);

	/* Debug control flags. Don't use unless you know what you're doing */
	int use_header_file_size;	/* Flag to determine if we should use
					 * file sizes from the header */
	int disable_lazy_load;	/* Disable lazy loading on this device */
	int wide_tnodes_disabled;	/* Set to disable wide tnodes */
	int disable_soft_del;	/* yaffs 1 only: Set to disable the use of
				 * softdeletion. */

	int defered_dir_update;	/* Set to defer directory updates */

#ifdef CONFIG_YAFFS_AUTO_UNICODE
	int auto_unicode;
#endif
	int always_check_erased;	/* 写chunk前总是检查块是否被擦 */

	int disable_summary;
	int disable_bad_block_marking;

};

struct yaffs_driver {
	int (*drv_write_chunk_fn) (struct yaffs_dev *dev, int nand_chunk,
				   const u8 *data, int data_len,
				   const u8 *oob, int oob_len);
	int (*drv_read_chunk_fn) (struct yaffs_dev *dev, int nand_chunk,
				   u8 *data, int data_len,
				   u8 *oob, int oob_len,
				   enum yaffs_ecc_result *ecc_result);
	int (*drv_erase_fn) (struct yaffs_dev *dev, int block_no);
	int (*drv_mark_bad_fn) (struct yaffs_dev *dev, int block_no);
	int (*drv_check_bad_fn) (struct yaffs_dev *dev, int block_no);
	int (*drv_initialise_fn) (struct yaffs_dev *dev);
	int (*drv_deinitialise_fn) (struct yaffs_dev *dev);
};

struct yaffs_tags_handler {
	int (*write_chunk_tags_fn) (struct yaffs_dev *dev,
				    int nand_chunk, const u8 *data,
				    const struct yaffs_ext_tags *tags);
	int (*read_chunk_tags_fn) (struct yaffs_dev *dev,
				   int nand_chunk, u8 *data,
				   struct yaffs_ext_tags *tags);

	int (*query_block_fn) (struct yaffs_dev *dev, int block_no,
			       enum yaffs_block_state *state,
			       u32 *seq_number);
	int (*mark_bad_fn) (struct yaffs_dev *dev, int block_no);
};

struct yaffs_dev {
	struct yaffs_param param;			/* 配置项 */
	struct yaffs_driver drv;			/* 驱动函数(nand读写和初始化) */
	struct yaffs_tags_handler tagger;	/* tags处理函数 */

	/* Context storage. Holds extra OS specific data for this device */

	void *os_context;					/* 操作系統上下文 */
	void *driver_context;				/* 驱动上下文 */

	struct list_head dev_list;			/* 设备链表 */

	int ll_init;						/* 初始化标志位 */
	/* Runtime parameters. Set up by YAFFS. */
	u32 data_bytes_per_chunk;

	/* Non-wide tnode stuff */
	u16 chunk_grp_bits;	/* Number of bits that need to be resolved if
				 * the tnodes are not wide enough.
				 */
	u16 chunk_grp_size;	/* == 2^^chunk_grp_bits */

	struct yaffs_tnode *tn_swap_buffer;

	/* Stuff to support wide tnodes */
	u32 tnode_width;
	u32 tnode_mask;
	u32 tnode_size;

	/* 用于chunk地址的计算 */
	u32 chunk_shift;	/* Shift value */
	u32 chunk_div;		/* Divisor after shifting: 1 for 2^n sizes */
	u32 chunk_mask;		/* Mask to use for power-of-2 case */

	int is_mounted;
	int read_only;
	int is_checkpointed;
	int swap_endian;	/* 需要大小端转换标志位 */

	/* chunk和块大小和偏移 */
	u32 internal_start_block;
	u32 internal_end_block;
	int block_offset;
	int chunk_offset;

	/* checkpoint检查点变量 */
	int checkpt_page_seq;	/* checkpoint chunk 序列号 */
	int checkpt_byte_count;
	int checkpt_byte_offs;
	u8 *checkpt_buffer;
	int checkpt_open_write;
	u32 blocks_in_checkpt;
	int checkpt_cur_chunk;
	int checkpt_cur_block;
	int checkpt_next_block;
	int *checkpt_block_list;
	u32 checkpt_max_blocks;
	u32 checkpt_sum;
	u32 checkpt_xor;

	int checkpoint_blocks_required;	/* 需要的存储checkpoint块数 */

	/* Block Info */
	struct yaffs_block_info *block_info;
	u8 *chunk_bits;		/* 正在使用的chunks位图 */
	u8 block_info_alt:1;	/* allocated using alternative alloc */
	u8 chunk_bits_alt:1;	/* allocated using alternative alloc */
	int chunk_bit_stride;	/* Number of bytes of chunk_bits per block.
				 * Must be consistent with chunks_per_block.
				 */

	int n_erased_blocks;
	int alloc_block;	/* Current block being allocated off */
	u32 alloc_page;
	int alloc_block_finder;	/* Used to search for next allocation block */

	/* Object and Tnode memory management */
	void *allocator;
	int n_obj;
	int n_tnodes;

	int n_hardlinks;

	struct yaffs_obj_bucket obj_bucket[YAFFS_NOBJECT_BUCKETS];
	u32 bucket_finder;

	int n_free_chunks;

	/* Garbage collection control */
	u32 *gc_cleanup_list;	/* objects to delete at the end of a GC. */
	u32 n_clean_ups;

	unsigned has_pending_prioritised_gc;	/* We think this device might
						have pending prioritised gcs */
	unsigned gc_disable;
	unsigned gc_block_finder;
	unsigned gc_dirtiest;
	unsigned gc_pages_in_use;
	unsigned gc_not_done;
	unsigned gc_block;
	unsigned gc_chunk;
	unsigned gc_skip;
	struct yaffs_summary_tags *gc_sum_tags;

	/* Special directories */
	struct yaffs_obj *root_dir;
	struct yaffs_obj *lost_n_found;

	int buffered_block;	/* Which block is buffered here? */
	int doing_buffered_block_rewrite;

	struct yaffs_cache *cache;
	int cache_last_use;

	/* Stuff for background deletion and unlinked files. */
	struct yaffs_obj *unlinked_dir;	/* Directory where unlinked and deleted
					 files live. */
	struct yaffs_obj *del_dir;	/* Directory where deleted objects are
					sent to disappear. */
	struct yaffs_obj *unlinked_deletion;	/* Current file being
							background deleted. */
	int n_deleted_files;	/* Count of files awaiting deletion; */
	int n_unlinked_files;	/* Count of unlinked files. */
	int n_bg_deletions;	/* Count of background deletions. */

	/* Temporary buffer management */
	struct yaffs_buffer temp_buffer[YAFFS_N_TEMP_BUFFERS];
	int max_temp;
	int temp_in_use;
	int unmanaged_buffer_allocs;
	int unmanaged_buffer_deallocs;

	/* yaffs2 runtime stuff */
	unsigned seq_number;	/* Sequence number of currently
					allocating block */
	unsigned oldest_dirty_seq;
	unsigned oldest_dirty_block;

	/* Block refreshing */
	int refresh_skip;	/* A skip down counter.
				 * Refresh happens when this gets to zero. */

	/* Dirty directory handling */
	struct list_head dirty_dirs;	/* List of dirty directories */

	/* Summary */
	int chunks_per_summary;
	struct yaffs_summary_tags *sum_tags;

	/* Statistics */
	u32 n_page_writes;
	u32 n_page_reads;
	u32 n_erasures;
	u32 n_bad_queries;
	u32 n_bad_markings;
	u32 n_erase_failures;
	u32 n_gc_copies;
	u32 all_gcs;
	u32 passive_gc_count;
	u32 oldest_dirty_gc_count;
	u32 n_gc_blocks;
	u32 bg_gcs;
	u32 n_retried_writes;
	u32 n_retired_blocks;
	u32 n_ecc_fixed;
	u32 n_ecc_unfixed;
	u32 n_tags_ecc_fixed;
	u32 n_tags_ecc_unfixed;
	u32 n_deletions;
	u32 n_unmarked_deletions;
	u32 refresh_count;
	u32 cache_hits;
	u32 tags_used;
	u32 summary_used;

};

/*
 * Checkpointing definitions.
 */

#define YAFFS_CHECKPOINT_VERSION	8

/* yaffs_checkpt_obj holds the definition of an object as dumped
 * by checkpointing.
 */


/*  Checkpint object bits in bitfield: offset, length */
#define CHECKPOINT_VARIANT_BITS		0, 3
#define CHECKPOINT_DELETED_BITS		3, 1
#define CHECKPOINT_SOFT_DEL_BITS	4, 1
#define CHECKPOINT_UNLINKED_BITS	5, 1
#define CHECKPOINT_FAKE_BITS		6, 1
#define CHECKPOINT_RENAME_ALLOWED_BITS	7, 1
#define CHECKPOINT_UNLINK_ALLOWED_BITS	8, 1
#define CHECKPOINT_SERIAL_BITS		9, 8

struct yaffs_checkpt_obj {
	int struct_type;
	u32 obj_id;
	u32 parent_id;
	int hdr_chunk;
	u32 bit_field;
	int n_data_chunks;
	loff_t size_or_equiv_obj;
};

/* The CheckpointDevice structure holds the device information that changes
 *at runtime and must be preserved over unmount/mount cycles.
 */
struct yaffs_checkpt_dev {
	int struct_type;
	int n_erased_blocks;
	int alloc_block;	/* Current block being allocated off */
	u32 alloc_page;
	int n_free_chunks;

	int n_deleted_files;	/* Count of files awaiting deletion; */
	int n_unlinked_files;	/* Count of unlinked files. */
	int n_bg_deletions;	/* Count of background deletions. */

	/* yaffs2 runtime stuff */
	unsigned seq_number;	/* Sequence number of currently
				 * allocating block */

};

struct yaffs_checkpt_validity {
	int struct_type;
	u32 magic;
	u32 version;
	u32 head;
};

struct yaffs_shadow_fixer {
	int obj_id;
	int shadowed_id;
	struct yaffs_shadow_fixer *next;
};

/* Structure for doing xattr modifications */
struct yaffs_xattr_mod {
	int set;		/* If 0 then this is a deletion */
	const YCHAR *name;
	const void *data;
	int size;
	int flags;
	int result;
};

/*----------------------- YAFFS Functions -----------------------*/

int yaffs_guts_initialise(struct yaffs_dev *dev);
void yaffs_deinitialise(struct yaffs_dev *dev);

int yaffs_get_n_free_chunks(struct yaffs_dev *dev);

int yaffs_rename_obj(struct yaffs_obj *old_dir, const YCHAR * old_name,
		     struct yaffs_obj *new_dir, const YCHAR * new_name);

int yaffs_unlink_obj(struct yaffs_obj *obj);

int yaffs_unlinker(struct yaffs_obj *dir, const YCHAR * name);
int yaffs_del_obj(struct yaffs_obj *obj);
struct yaffs_obj *yaffs_retype_obj(struct yaffs_obj *obj,
				   enum yaffs_obj_type type);


int yaffs_get_obj_name(struct yaffs_obj *obj, YCHAR * name, int buffer_size);
loff_t yaffs_get_obj_length(struct yaffs_obj *obj);
int yaffs_get_obj_inode(struct yaffs_obj *obj);
unsigned yaffs_get_obj_type(struct yaffs_obj *obj);
int yaffs_get_obj_link_count(struct yaffs_obj *obj);

/* File operations */
int yaffs_file_rd(struct yaffs_obj *obj, u8 * buffer, loff_t offset,
		  int n_bytes);
int yaffs_wr_file(struct yaffs_obj *obj, const u8 * buffer, loff_t offset,
		  int n_bytes, int write_trhrough);
int yaffs_resize_file(struct yaffs_obj *obj, loff_t new_size);

struct yaffs_obj *yaffs_create_file(struct yaffs_obj *parent,
				    const YCHAR *name, u32 mode, u32 uid,
				    u32 gid);

int yaffs_flush_file(struct yaffs_obj *in,
		     int update_time,
		     int data_sync,
		     int discard_cache);

/* Flushing and checkpointing */
void yaffs_flush_whole_cache(struct yaffs_dev *dev, int discard);

int yaffs_checkpoint_save(struct yaffs_dev *dev);
int yaffs_checkpoint_restore(struct yaffs_dev *dev);

/* Directory operations */
struct yaffs_obj *yaffs_create_dir(struct yaffs_obj *parent, const YCHAR *name,
				   u32 mode, u32 uid, u32 gid);
struct yaffs_obj *yaffs_find_by_name(struct yaffs_obj *the_dir,
				     const YCHAR *name);
struct yaffs_obj *yaffs_find_by_number(struct yaffs_dev *dev, u32 number);

/* Link operations */
struct yaffs_obj *yaffs_link_obj(struct yaffs_obj *parent, const YCHAR *name,
				 struct yaffs_obj *equiv_obj);

struct yaffs_obj *yaffs_get_equivalent_obj(struct yaffs_obj *obj);

/* Symlink operations */
struct yaffs_obj *yaffs_create_symlink(struct yaffs_obj *parent,
				       const YCHAR *name, u32 mode, u32 uid,
				       u32 gid, const YCHAR *alias);
YCHAR *yaffs_get_symlink_alias(struct yaffs_obj *obj);

/* Special inodes (fifos, sockets and devices) */
struct yaffs_obj *yaffs_create_special(struct yaffs_obj *parent,
				       const YCHAR *name, u32 mode, u32 uid,
				       u32 gid, u32 rdev);

int yaffs_set_xattrib(struct yaffs_obj *obj, const YCHAR *name,
		      const void *value, int size, int flags);
int yaffs_get_xattrib(struct yaffs_obj *obj, const YCHAR *name, void *value,
		      int size);
int yaffs_list_xattrib(struct yaffs_obj *obj, char *buffer, int size);
int yaffs_remove_xattrib(struct yaffs_obj *obj, const YCHAR *name);

/* Special directories */
struct yaffs_obj *yaffs_root(struct yaffs_dev *dev);
struct yaffs_obj *yaffs_lost_n_found(struct yaffs_dev *dev);

void yaffs_handle_defered_free(struct yaffs_obj *obj);

void yaffs_update_dirty_dirs(struct yaffs_dev *dev);

int yaffs_bg_gc(struct yaffs_dev *dev, unsigned urgency);

/* Debug dump  */
int yaffs_dump_obj(struct yaffs_obj *obj);

void yaffs_guts_test(struct yaffs_dev *dev);
int yaffs_guts_ll_init(struct yaffs_dev *dev);


/* A few useful functions to be used within the core files*/
int yaffs_check_block_erased(struct yaffs_dev *dev, int block);
void yaffs_chunk_del(struct yaffs_dev *dev, int chunk_id, int mark_flash,
		     int lyn);
int yaffs_check_ff(u8 *buffer, int n_bytes);
void yaffs_handle_chunk_error(struct yaffs_dev *dev,
			      struct yaffs_block_info *bi);

u8 *yaffs_get_temp_buffer(struct yaffs_dev *dev);
void yaffs_release_temp_buffer(struct yaffs_dev *dev, u8 *buffer);

struct yaffs_obj *yaffs_find_or_create_by_number(struct yaffs_dev *dev,
						 int number,
						 enum yaffs_obj_type type);
int yaffs_put_chunk_in_file(struct yaffs_obj *in, int inode_chunk,
			    int nand_chunk, int in_scan);
void yaffs_set_obj_name(struct yaffs_obj *obj, const YCHAR *name);
void yaffs_set_obj_name_from_oh(struct yaffs_obj *obj,
				const struct yaffs_obj_hdr *oh);
void yaffs_add_obj_to_dir(struct yaffs_obj *directory, struct yaffs_obj *obj);
YCHAR *yaffs_clone_str(const YCHAR *str);
void yaffs_link_fixup(struct yaffs_dev *dev, struct list_head *hard_list);
void yaffs_block_became_dirty(struct yaffs_dev *dev, int block_no);
int yaffs_update_oh(struct yaffs_obj *in, const YCHAR *name,
		    int force, int is_shrink, int shadows,
		    struct yaffs_xattr_mod *xop);
void yaffs_handle_shadowed_obj(struct yaffs_dev *dev, int obj_id,
			       int backward_scanning);
int yaffs_check_alloc_available(struct yaffs_dev *dev, int n_chunks);
struct yaffs_tnode *yaffs_get_tnode(struct yaffs_dev *dev);
struct yaffs_tnode *yaffs_add_find_tnode_0(struct yaffs_dev *dev,
					   struct yaffs_file_var *file_struct,
					   u32 chunk_id,
					   struct yaffs_tnode *passed_tn);

int yaffs_do_file_wr(struct yaffs_obj *in, const u8 *buffer, loff_t offset,
		     int n_bytes, int write_trhrough);
void yaffs_resize_file_down(struct yaffs_obj *obj, loff_t new_size);
void yaffs_skip_rest_of_block(struct yaffs_dev *dev);

int yaffs_count_free_chunks(struct yaffs_dev *dev);

struct yaffs_tnode *yaffs_find_tnode_0(struct yaffs_dev *dev,
				       struct yaffs_file_var *file_struct,
				       u32 chunk_id);

u32 yaffs_get_group_base(struct yaffs_dev *dev, struct yaffs_tnode *tn,
			 unsigned pos);

int yaffs_is_non_empty_dir(struct yaffs_obj *obj);

int yaffs_guts_format_dev(struct yaffs_dev *dev);

void yaffs_addr_to_chunk(struct yaffs_dev *dev, loff_t addr,
				int *chunk_out, u32 *offset_out);
/*
 * Marshalling functions to get loff_t file sizes into and out of
 * object headers.
 */
void yaffs_oh_size_load(struct yaffs_dev *dev, struct yaffs_obj_hdr *oh,
			loff_t fsize, int do_endian);
loff_t yaffs_oh_to_size(struct yaffs_dev *dev, struct yaffs_obj_hdr *oh,
			int do_endian);
loff_t yaffs_max_file_size(struct yaffs_dev *dev);

/*
 * Debug function to count number of blocks in each state
 * NB Needs to be called with correct number of integers
 */

void yaffs_count_blocks_by_state(struct yaffs_dev *dev, int bs[10]);

int yaffs_find_chunk_in_file(struct yaffs_obj *in, int inode_chunk,
				    struct yaffs_ext_tags *tags);

/*
 * Define LOFF_T_32_BIT if a 32-bit LOFF_T is being used.
 * Not serious if you get this wrong - you might just get some warnings.
*/

#ifdef  LOFF_T_32_BIT
#define FSIZE_LOW(fsize) (fsize)
#define FSIZE_HIGH(fsize) 0
#define FSIZE_COMBINE(high, low) (low)
#else
#define FSIZE_LOW(fsize) ((fsize) & 0xffffffff)
#define FSIZE_HIGH(fsize)(((fsize) >> 32) & 0xffffffff)
#define FSIZE_COMBINE(high, low) ((((loff_t) (high)) << 32) | \
					(((loff_t) (low)) & 0xFFFFFFFF))
#endif


#endif
