/**
 * @copyright
 *
 * Tencent is pleased to support the open source community by making IoT Hub available.
 * Copyright(C) 2018 - 2021 THL A29 Limited, a Tencent company.All rights reserved.
 *
 * Licensed under the MIT License(the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License at
 * http://opensource.org/licenses/MIT
 *
 * Unless required by applicable law or agreed to in writing, software distributed under the License is
 * distributed on an "AS IS" basis, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
 * either express or implied. See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * @file HAL_OS_linux.c
 * @brief Linux os api
 * @author albertai (albertai@tencent.com)
 * @version 1.0
 * @date 2024-11-14
 *
 * @par Change Log:
 */

#include <errno.h>
#include <memory.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
// #include <sys/types.h>
// #include <sys/msg.h>
#include <unistd.h>
#include "HAL_Platform.h"

#include <os.h>

/**
 * @brief platform-dependent thread routine/entry function
 *
 * @param[in,out] ptr
 * @return NULL
 */
static void *_HAL_thread_func_wrapper_(void *ptr)
{
    ThreadParams *params = (ThreadParams *)ptr;

    params->thread_func(params->user_arg);

    pthread_detach(pthread_self());
    pthread_exit(0);
    return NULL;
}

/**
 * @brief platform-dependent thread create function
 *
 * @param[in,out] params params to create thread @see ThreadParams
 * @return @see IotReturnCode
 */
int32_t HAL_ThreadCreate(ThreadParams *params)
{
    if (NULL == params) {
        return ERR_CODE_INVALIDPARAM;
    }

    const pthread_attr_t attr = {
        .schedpolicy = SCHED_OTHER,
        .schedparam.sched_priority = OS_priority_normal,
        .detachstate = PTHREAD_CREATE_JOINABLE,
        .stacksize = params->stack_size,
    };

    int32_t rc = pthread_create((pthread_t *)&params->thread_id, &attr, _HAL_thread_func_wrapper_, (void *)params);
    if (rc) {
        HAL_Printf("%s: pthread_create failed: %d\n", __FUNCTION__, rc);
        return ERR_CODE_GENERALFAIL;
    }

    return ERR_CODE_SUCCESS;
}

/**
 * @brief platform-dependent get thread id
 *
 * @return @see IotReturnCode
 */
int32_t HAL_ThreadId(uint64_t *thread_id)
{
    *thread_id = pthread_self();
    return ERR_CODE_SUCCESS;
}

/**
 * @brief platform-dependent thread destroy function.
 *
 */
void HAL_ThreadDestroy(uint64_t thread_id)
{

}

/**
 * @brief Malloc from heap.
 *
 * @param[in] size size to malloc
 * @param[out] ptr buf point to malloc
 * @return @see IotReturnCode
 */
int32_t HAL_Malloc(uint32_t size, void **ptr)
{
    void *ptr_tmp = malloc(size);
    if (ptr_tmp) {
        memset(ptr_tmp, 0, size);
        *ptr = ptr_tmp;
        return ERR_CODE_SUCCESS;
    } else {
        return ERR_CODE_OS_NOMEM;
    }
}

/**
 * @brief Free buffer malloced by HAL_Malloc.
 *
 * @param[in] ptr
 */
void HAL_Free(void *ptr)
{
    if (ptr) {
        free(ptr);
    }
    ptr = NULL;
}

/**
 * @brief Printf with format.
 *
 * @param[in] fmt format
 */
void HAL_Printf(const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);

    fflush(stdout);
}

/**
 * @brief Snprintf with format.
 *
 * @param[out] str buffer to save
 * @param[in] len buffer len
 * @param[in] fmt format
 * @return @see IotReturnCode
 */
int32_t HAL_Snprintf(char *str, const uint32_t str_len, uint32_t *dst_len, const char *fmt, ...)
{
    va_list args;
    int32_t     rc;

    va_start(args, fmt);
    rc = vsnprintf(str, str_len, fmt, args);
    va_end(args);
    if (rc > 0) {
        *dst_len = rc;
        return ERR_CODE_SUCCESS;
    }
    return ERR_CODE_GENERALFAIL;
}

/**
 * @brief Sleep for ms
 *
 * @param[in] ms ms to sleep
 */
void HAL_SleepMs(uint32_t ms)
{
    usleep(1000 * ms);
}

/**
 * @brief Mutex create.
 *
 * @param[out] mutex pointed to created mutex
 * @return @see IotReturnCode
 */
int32_t HAL_MutexCreate(void **mutex)
{
    int32_t err_num;
    int32_t rc;
    pthread_mutex_t *mutex_tmp = NULL;

    rc = HAL_Malloc(sizeof(pthread_mutex_t), (void *)&mutex_tmp);
    if (rc) {
        return ERR_CODE_OS_NOMEM;
    }

    err_num = pthread_mutex_init(mutex_tmp, NULL);

    if (err_num) {
        HAL_Printf("%s: create mutex failed\n", __FUNCTION__);
        HAL_Free(mutex_tmp);
        return ERR_CODE_OS_MUTEXCREATEFAIL;
    }
    *mutex = mutex_tmp;

    return ERR_CODE_SUCCESS;
}

/**
 * @brief Mutex destroy.
 *
 * @param[in] mutex pointer to mutex
 * @return NULL
 */
void HAL_MutexDestroy(void *mutex)
{
    if (!(mutex)) {
        return;
    }
    int32_t err_num = pthread_mutex_destroy((pthread_mutex_t *)(mutex));
    if (err_num) {
        HAL_Printf("%s: destroy mutex failed\n", __FUNCTION__);
    }

    HAL_Free(mutex);
}

/**
 * @brief Mutex lock.
 *
 * @param[in] mutex pointer to mutex
 */
void HAL_MutexLock(void *mutex)
{
    if (!(mutex)) {
        HAL_Printf("%s: mutex not create\n", __FUNCTION__);
        return;
    }
    int32_t err_num = pthread_mutex_lock((pthread_mutex_t *)(mutex));
    if (err_num) {
        HAL_Printf("%s: lock mutex failed\n", __FUNCTION__);
    }
}

/**
 * @brief Mutex try lock.
 *
 * @param[in] mutex pointer to mutex
 * @return @see IotReturnCode
 */
int32_t HAL_MutexTryLock(void *mutex)
{
    if (!mutex) {
        return ERR_CODE_INVALIDPARAM;
    }
    if(!pthread_mutex_trylock((pthread_mutex_t *)mutex)) {
        return ERR_CODE_SUCCESS;
    } else {
        return ERR_CODE_OS_MUTEXLOCKFAIL;
    }
}

/**
 * @brief Mutex unlock.
 *
 * @param[in,out] mutex pointer to mutex
 */
void HAL_MutexUnlock(void *mutex)
{
    if (!(mutex)) {
        HAL_Printf("%s: mutex not create\n", __FUNCTION__);
        return;
    }
    int32_t err_num;
    if (0 != (err_num = pthread_mutex_unlock((pthread_mutex_t *)(mutex)))) {
        HAL_Printf("%s: unlock mutex failed\n", __FUNCTION__);
    }
}

/**
 * @brief platform-dependent semaphore create function.
 *
 * @param[out] sem pointered to created sem
 * @return @see IotReturnCode
 */
int32_t HAL_SemaphoreCreate(void **sem)
{
    sem_t *sem_tmp = (sem_t *)malloc(sizeof(sem_t));
    if (!sem_tmp) {
        return ERR_CODE_OS_NOMEM;
    }

    if (sem_init(sem_tmp, 0, 0)) {
        free(sem_tmp);
        return ERR_CODE_OS_SEMCREATEFAIL;
    }
    *sem = sem_tmp;

    return ERR_CODE_SUCCESS;
}

/**
 * @brief platform-dependent semaphore destory function.
 *
 * @param[in] sem pointer to semaphore
 */
void HAL_SemaphoreDestroy(void *sem)
{
    sem_destroy((sem_t *)(sem));
    free(sem);
}

/**
 * @brief platform-dependent semaphore post function.
 *
 * @param[in] sem pointer to semaphore
 */
void HAL_SemaphorePost(void *sem)
{
    sem_post((sem_t *)(sem));
}

/**
 * @brief platform-dependent semaphore wait function.
 *
 * @param[in] sem pointer to semaphore
 * @param[in] timeout_ms wait timeout
 * @return @see IotReturnCode
 */
int32_t HAL_SemaphoreWait(void *sem, uint32_t timeout_ms)
{
    if (0xFFFFFFFF == timeout_ms) {
        sem_wait(sem);
        return ERR_CODE_SUCCESS;
    } else {
        struct timespec ts;
        int32_t             s;
        /* Restart if interrupted by handler */
        do {
            if (clock_gettime(CLOCK_REALTIME, &ts) == -1) {
                return ERR_CODE_GENERALFAIL;
            }

            s = 0;
            ts.tv_nsec += (timeout_ms % 1000) * 1000000;
            if (ts.tv_nsec >= 1000000000) {
                ts.tv_nsec -= 1000000000;
                s = 1;
            }

            ts.tv_sec += timeout_ms / 1000 + s;

        } while (((s = sem_timedwait(sem, &ts)) != 0) && errno == EINTR);

        return s ? ERR_CODE_GENERALFAIL : ERR_CODE_SUCCESS;
    }
}