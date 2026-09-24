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
 * @file HAL_Timer_linux.c
 * @brief Linux timer function
 * @author fancyxu (fancyxu@tencent.com)
 * @version 1.0
 * @date 2021-05-31
 *
 * @par Change Log:
 * <table>
 * <tr><th>Date       <th>Version <th>Author    <th>Description
 * <tr><td>2021-05-31 <td>1.0     <td>fancyxu   <td>first commit
 * </table>
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/time.h>
#include <time.h>
#include <signal.h>

#include "HAL_Platform.h"

#include <driver/systick.h>
#include <driver/rtc.h>

/**
 * @brief time format string
 * @param[out] time_str store time formated str. time format "2021-05-31 15:58:46"
 * @return IotReturnCode
 */
int32_t HAL_TimeCurrentStr(char *time_str)
{
    struct rtc_time tm;
    rtc_get_current_tm(&tm);
    snprintf(time_str, 20, "%04d-%02d-%02d %02d:%02d:%02d",
             tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
             tm.tm_hour, tm.tm_min, tm.tm_sec);

    return ERR_CODE_SUCCESS;
}

/**
 * @brief Get utc time ms timestamp.
 * @param[out] ms timestamp
 * @return @see IotReturnCode
 */
int32_t HAL_TimeCurrentMs(uint64_t *ms)
{
    uint64_t sec = rtc_get_current_time();
    uint64_t ms_part = systick_get_time_ms() % 1000;
    *ms = sec * 1000 + ms_part;

    return ERR_CODE_SUCCESS;
}

/**
 * @brief Set system time using ms timestamp
 *
 * @param[in] timestamp_ms
 * @return @see IotReturnCode
 */
int32_t HAL_TimeSystimeMsSet(uint64_t timestamp_ms)
{
    rtc_set_time(timestamp_ms / 1000);
    return ERR_CODE_SUCCESS;
}

/**
 * @brief Get system tick
 *
 * @param[out] system tick
 * @return @see IotReturnCode
 */
int32_t HAL_TimeSysTickMsGet(uint64_t *sys_tick_ms)
{
    *sys_tick_ms = systick_get_time_us() / 1000;
    return ERR_CODE_SUCCESS;
}

#ifdef __cplusplus
}
#endif
