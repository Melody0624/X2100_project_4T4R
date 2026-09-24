/*
 * Copyright (c) 2006-2018, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2012-03-30     prife        the first version
 */

#ifndef DFS_UFFS_H_
#define DFS_UFFS_H_

#include "uffs_config.h"
#include "uffs/uffs_public.h"

/* the UFFS ECC mode opitons  */
#ifndef UFFS_ECC_MODE
#define UFFS_ECC_MODE      3
#endif

/*
 * UFFS_ECC_MODE:
 * 0, Do not use ECC
 * 1, UFFS calculate the ECC
 * 2, Flash driver(or by hardware) calculate the ECC
 * 3, Hardware calculate the ECC and automatically write to spare.
 */
#if UFFS_ECC_MODE == 0
#define CONFIG_UFFS_ECC_MODE UFFS_ECC_NONE
#elif UFFS_ECC_MODE == 1
#define CONFIG_UFFS_ECC_MODE UFFS_ECC_SOFT
#elif UFFS_ECC_MODE == 2
#define CONFIG_UFFS_ECC_MODE UFFS_ECC_HW
#elif UFFS_ECC_MODE == 3
#define CONFIG_UFFS_ECC_MODE UFFS_ECC_HW_AUTO
#endif

/* #define CONFIG_UFFS_ECC_MODE  UFFS_ECC_HW_AUTO */
/* #define CONFIG_UFFS_ECC_MODE  UFFS_ECC_SOFT */
/* #define CONFIG_UFFS_ECC_MODE  UFFS_ECC_NONE */

/* enable this ,you need provide a mark_badblock/check_block funciton */
/* #define UFFS_USE_CHECK_MARK_FUNCITON */

#if CONFIG_UFFS_ECC_MODE == UFFS_ECC_SOFT      /* let uffs do soft ecc */
#define CONFIG_UFFS_LAYOUT    UFFS_LAYOUT_UFFS /* UFFS_LAYOUT_FLASH */

#elif CONFIG_UFFS_ECC_MODE == UFFS_ECC_HW_AUTO /* nand driver make ecc and do ecc correct */
#define CONFIG_UFFS_LAYOUT    UFFS_LAYOUT_FLASH

#elif CONFIG_UFFS_ECC_MODE == UFFS_ECC_NONE
#define CONFIG_UFFS_LAYOUT    UFFS_LAYOUT_UFFS /* UFFS_LAYOUT_FLASH */

#else
#error "uffs under rt-thread do not support this ECC mode"
#endif /* CONFIG_UFFS_ECC_MODE */

#if (!CONFIG_USE_STATIC_MEMORY_ALLOCATOR)  && (CONFIG_USE_SYSTEM_MEMORY_ALLOCATOR)
#define UFFS_MEMORY_ALLOCATOR  1  /* use system memory allocator */
#elif (CONFIG_USE_STATIC_MEMORY_ALLOCATOR) && (!CONFIG_USE_SYSTEM_MEMORY_ALLOCATOR)
#define UFFS_MEMORY_ALLOCATOR  0  /* use static memory allocator */
#else
#error "UFFS: CONFIG_USE_STATIC_MEMORY_ALLOCATOR ,CONFIG_USE_SYSTEM_MEMORY_ALLOCATOR are invalid!"
#endif

#if CONFIG_USE_STATIC_MEMORY_ALLOCATOR > 0
#error "dfs_uffs only support CONFIG_USE_SYSTEM_MEMORY_ALLOCATOR"
#endif

// #if defined(CONFIG_UFFS_AUTO_LAYOUT_USE_MTD_SCHEME)
// #error "dfs_uffs not support CONFIG_UFFS_AUTO_LAYOUT_USE_MTD_SCHEME"
// #endif

#if (CONFIG_UFFS_ECC_MODE == UFFS_ECC_HW_AUTO) && (CONFIG_UFFS_LAYOUT != UFFS_LAYOUT_FLASH)
#error "when use UFFS_ECC_HW_AUTO, you must use UFFS_LAYOUT_FLASH"

#elif (CONFIG_UFFS_ECC_MODE == UFFS_ECC_SOFT) && (CONFIG_UFFS_LAYOUT != UFFS_LAYOUT_UFFS)
#warning "when use UFFS_ECC_SOFT, it is recommended to use UFFS_LAYOUT_UFFS"
#endif

extern uffs_FlashOps nand_ops;

extern void uffs_setup_storage(
    struct uffs_StorageAttrSt *attr,
    struct mtd_nand_device * nand);

extern int dfs_uffs_init(void);
#endif /* DFS_UFFS_H_ */
