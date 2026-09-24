/*
 * YAFFS: Yet Another Flash File System. A NAND-flash specific file system.
 *
 * Copyright (C) 2002-2011 Aleph One Ltd.
 *   for Toby Churchill Ltd and Brightstar Engineering
 *
 * Created by Charles Manning <charles@aleph1.co.uk>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */


#include "yaffs/yaffs_guts.h"
#include "yaffs/yaffs_trace.h"

#include "pthread.h"
#include <assert.h>
#include <unistd.h>
#include <malloc.h>
#include <errno.h>

unsigned int yaffs_trace_mask =
    YAFFS_TRACE_ALWAYS |
    // YAFFS_TRACE_SCAN |
    // YAFFS_TRACE_GC |
    // YAFFS_TRACE_ERASE |
    YAFFS_TRACE_ERROR |
    // YAFFS_TRACE_TRACING |
    // YAFFS_TRACE_ALLOCATE |
    YAFFS_TRACE_BAD_BLOCKS |
    // YAFFS_TRACE_VERIFY |
    // YAFFS_TRACE_SCAN_DEBUG |
    // YAFFS_TRACE_CHECKPOINT |
    // YAFFS_TRACE_MOUNT |
    // YAFFS_TRACE_VERIFY_FULL |
    // YAFFS_TRACE_VERIFY_NAND |
    // YAFFS_TRACE_MTD |
    // YAFFS_TRACE_DELETION |
    // YAFFS_TRACE_GC_DETAIL |
    // YAFFS_TRACE_NANDACCESS |
    // YAFFS_TRACE_WRITE |
    // YAFFS_TRACE_BACKGROUND |
    0;

static int yaffsfs_lastError;
static pthread_mutex_t mutex;
static pthread_t bc_gc_thread;

static void *bg_gc_func(void *dummy)
{
	struct yaffs_dev *dev;
	int urgent = 0;
	int result;
	int next_urgent;

	(void)dummy;

	/* Sleep for a bit to allow start up */
	sleep(2);


	while (1) {
		/* Iterate through devices, do bg gc updating ungency */
		yaffs_dev_rewind();
		next_urgent = 0;

		while ((dev = yaffs_next_dev()) != NULL) {
			result = yaffs_do_background_gc_reldev(dev, urgent);

			/* result is 1 if more than half the free space is
			 * erased.
			 * If less than half the free space is erased then it is
			 * worth doing another background_gc operation sooner.
			 */
			if (result == 0)
				next_urgent = 1;
		}

		urgent = next_urgent;

		if (next_urgent)
			sleep(2);
		else
			sleep(10);
	}

	/* Don't ever return. */
	return NULL;
}

void yaffsfs_SetError(int err)
{
    //Do whatever to set error
    yaffsfs_lastError = err;
    errno = err;
}

int yaffsfs_GetLastError(void)
{
    return yaffsfs_lastError;
}

void yaffsfs_Lock(void)
{
    pthread_mutex_lock(&mutex);
}

void yaffsfs_Unlock(void)
{
    pthread_mutex_unlock(&mutex);
}

void yaffsfs_LockInit(void)
{
    pthread_mutex_init(&mutex, NULL);

	pthread_create(&bc_gc_thread, NULL, bg_gc_func, NULL);
}

u32 yaffsfs_CurrentTime(void)
{
    return 0;
}

void *yaffsfs_malloc(size_t size)
{
    return malloc(size);
}

void yaffsfs_free(void *ptr)
{
    free(ptr);
}

void yaffsfs_OSInitialisation(void)
{
    yaffsfs_LockInit();
}

int yaffsfs_CheckMemRegion(const void *addr, size_t size, int write_request)
{
    (void) size;
    (void) write_request;

    if(!addr)
        return -1;
    return 0;
}

void yaffs_bug_fn(const char *file_name, int line_no)
{
    printf("yaffs bug detected %s:%d\n", file_name, line_no);
    assert(0);
}