/**
 * @copyright
 *
 * Tencent is pleased to support the open source community by making IoT Hub available.
 * Copyright(C) 2018 - 2022 THL A29 Limited, a Tencent company.All rights reserved.
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
 * @file wx_speaker_ext_api.c
 * @brief
 * @author fancyxu (fancyxu@tencent.com)
 * @version 1.0
 * @date 2022-01-24
 *
 * @par Change Log:
 * <table>
 * <tr><th>Date       <th>Version <th>Author    <th>Description
 * <tr><td>2022-01-24 <td>1.0     <td>fancyxu   <td>first commit
 * </table>
 */

#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <stddef.h>
#include <time.h>
#include <sys/time.h>
#include <dfs.h>
#include <dfs_fs.h>
#include <dfs_file.h>
#include <dfs_posix.h>
#include <common.h>
#include <assert.h>
#include <stdlib.h>
#include <math.h>
#include <os.h>
#include <libmad/mad.h>
#include <driver/pcm.h>

#include "HAL_Platform.h"
// #include "utils_log.h"

#include <driver/efuse.h>
#include <driver/sfc_nand.h>
#include <driver/sfc_nor.h>
#include <driver/mmc_device.h>

#define PICTURE_DATA "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"

/**************************************************************************************
 * device network
 **************************************************************************************/
/**
 * @brief network(4g current) connect
 * @return @see IotReturnCode
 */
int32_t HAL_4GConnect(void)
{
    Log_d("network connect ok");
    return ERR_CODE_SUCCESS;
}

/**
 * @brief network disconnect
 * @return @see IotReturnCode
 */
void HAL_4GDisconnect(void)
{
    Log_d("network disconnect ok");
}

/**
 * @brief network(4g current) connect status
 * @return @see IotReturnCode
 */
int32_t HAL_4GConnectStatusGet(uint8_t *basestation_attached, uint8_t *pdp_acitved, uint8_t *sim_ready)
{
    *basestation_attached = 1;
    *pdp_acitved = 1;
    *sim_ready = 1;
    return ERR_CODE_SUCCESS;
}

/**************************************************************************************
 * device info
 **************************************************************************************/
/**
 * @brief get remain fs space(byte)
 * @param [out] availspace fs left space
 * @return @see IotReturnCode
 */
int32_t HAL_FsAvailapaceGet(uint32_t *availspace)
{
    *availspace = 10000000;
    return ERR_CODE_SUCCESS;
}

/**
 * @brief get battery(0~100)
 * @param [out] battery battery level
 * @return @see IotReturnCode
 */
int32_t HAL_BatteryGet(uint32_t *battery)
{
    *battery = 90;
    return ERR_CODE_SUCCESS;
}

/**
 * @brief get network type 1 : wifi 2 : 2G 4 : 4G 5 : 5G
 * @param [out] network_type network type
 * @return @see IotReturnCode
 */
int32_t HAL_NetworkTypeGet(NetworkType *network_type)
{
    *network_type = NETWORK_TYPE_4G;
    return ERR_CODE_SUCCESS;
}

/**
 * @brief get signal level(csq, from 0 to 31)
 * @param [out] signal_level csq value
 * @return @see IotReturnCode
 */
int32_t HAL_SignalLevelGet(uint32_t *signal_level)
{
    *signal_level = 31;
    return ERR_CODE_SUCCESS;
}

/**
 * @brief get reamin ram(byte)
 * @param [out] ram_rest ram left
 * @return @see IotReturnCode
 */
int32_t HAL_RamRestGet(uint32_t *ram_rest)
{
    *ram_rest = 1000000;
    return ERR_CODE_SUCCESS;
}

/**
 * @brief get imei
 *
 * @param[out] dst_buf buf to store imei value
 * @param[in] buf_len buf size
 * @return @see IotReturnCode
 */
int32_t HAL_ImeiGet(char *dst_buf, uint32_t buf_len)
{
    if (dst_buf == NULL || buf_len == 0) {
        Log_e("null input!");
        return ERR_CODE_INVALIDPARAM;
    }

    strncpy(dst_buf, "867521057825389", buf_len);

    return ERR_CODE_SUCCESS;
}

/**
 * @brief get imsi
 *
 * @param[in] dst_buf buf to store imsi value
 * @param[in] buf_len buf size
 * @return @see IotReturnCode
 */
int32_t HAL_ImsiGet(char *dst_buf, uint32_t buf_len)
{
    if (dst_buf == NULL || buf_len == 0) {
        Log_e("null input!");
        return ERR_CODE_INVALIDPARAM;
    }

    strncpy(dst_buf, "460049417500673", buf_len);
    return ERR_CODE_SUCCESS;
}

/**
 * @brief get iccid
 *
 * @param[in] dst_buf buf to store imsi value
 * @param[in] buf_len buf size
 * @return @see IotReturnCode
 */
int32_t HAL_IccidGet(char *dst_buf, uint32_t buf_len)
{
    if (dst_buf == NULL || buf_len == 0) {
        Log_e("null input!");
        return ERR_CODE_INVALIDPARAM;
    }

    strncpy(dst_buf, "89860474192090950673", buf_len);
    return ERR_CODE_SUCCESS;
}

/**
 * @brief get location info
 *
 * @param[out] base_station_info buf to store location info
 * @return @see IotReturnCode
 */
int32_t HAL_LocationInfoGet(BaseStationInfo *base_station_info)
{
    if (base_station_info == NULL) {
        Log_e("null input!");
        return ERR_CODE_INVALIDPARAM;
    }

    base_station_info->cell_id = 205555073;
    base_station_info->location_area_code = 9382;
    base_station_info->mobile_country_code = 460;
    base_station_info->mobile_network_code = 0;
    base_station_info->signal_strength = -55;

    return ERR_CODE_SUCCESS;
}
/**************************************************************************************
 * device control operations
 **************************************************************************************/
/**
 * @brief reboot system
 *
 * @return NULL
 */
void HAL_SystemReboot(void)
{
    Log_d("reboot system");
}

/**
 * @brief power off system
 *
 * @return NULL
 */
void HAL_SystemPoweroff(void)
{
    Log_d("power off system");
}

/**
 * @brief get system power on type
 * @param[out] poweron_type power on type
 *
 * @return @see IotReturnCode
 */
int32_t HAL_SystemPoweronTypeGet(PoweronType *poweron_type)
{
    *poweron_type = POWERON_TYPE_KEYPRESS;
    return ERR_CODE_SUCCESS;
}

/**************************************************************************************
 * audio operations
 **************************************************************************************/

 //#define HAL_AUDIO_SYNC

struct audio_resources {
    struct mutex lock;
    thread_waiter_t waiter;
#ifdef HAL_AUDIO_SYNC
    thread_waiter_t waiter_complete;
#endif

    struct pcm_device *dev;

    int mode; //0 : pcm, 1 : mp3

    void *buf;
    int size;
    volatile int playing;
};

static struct audio_resources audio_res;

struct mp3_decode_ctx {
    const unsigned char *start;
    unsigned long length;
    struct pcm_device *dev;
    int write_error;
};

static inline signed int mp3_scale(mad_fixed_t sample)
{
    sample += (1L << (MAD_F_FRACBITS - 16));

    if (sample >= MAD_F_ONE)
        sample = MAD_F_ONE - 1;
    else if (sample < -MAD_F_ONE)
        sample = -MAD_F_ONE;

    return sample >> (MAD_F_FRACBITS + 1 - 16);
}

static enum mad_flow mp3_input(void *data, struct mad_stream *stream)
{
    struct mp3_decode_ctx *ctx = data;

    if (!ctx->length)
        return MAD_FLOW_STOP;

    mad_stream_buffer(stream, ctx->start, ctx->length);
    ctx->length = 0;

    return MAD_FLOW_CONTINUE;
}

static enum mad_flow mp3_output(void *data,
                               struct mad_header const *header,
                               struct mad_pcm *pcm)
{
    struct mp3_decode_ctx *ctx = data;
    unsigned int nsamples = pcm->length;
    unsigned int channels = (pcm->channels == 2) ? 2 : 1;
    static signed short mono_buf[1152];
    unsigned int i;
    int ret;

    (void)header;

    if (nsamples > ARRAY_SIZE(mono_buf))
        nsamples = ARRAY_SIZE(mono_buf);

    if (channels == 1) {
        const mad_fixed_t *left_ch = pcm->samples[0];

        for (i = 0; i < nsamples; i++)
            mono_buf[i] = (signed short)mp3_scale(left_ch[i]);
    } else {
        const mad_fixed_t *left_ch = pcm->samples[0];
        const mad_fixed_t *right_ch = pcm->samples[1];

        for (i = 0; i < nsamples; i++) {
            int left = mp3_scale(left_ch[i]);
            int right = mp3_scale(right_ch[i]);

            mono_buf[i] = (signed short)((left + right) / 2);
        }
    }

    ret = pcm_write_frame(ctx->dev, mono_buf, nsamples);
    if (ret < 0) {
        ctx->write_error = ret;
        return MAD_FLOW_STOP;
    }

    return MAD_FLOW_CONTINUE;
}

static enum mad_flow mp3_error(void *data, struct mad_stream *stream, struct mad_frame *frame)
{
    struct mp3_decode_ctx *ctx = data;
    const unsigned char *start = ctx->start;

    (void)frame;

    fprintf(stderr, "decoding error 0x%04x (%s) at byte offset %td\n",
            stream->error, mad_stream_errorstr(stream),
            (ptrdiff_t)(stream->this_frame - start));

    return MAD_FLOW_CONTINUE;
}

static int decode_mp3_to_pcm(struct pcm_device *dev, const unsigned char *start, unsigned long length)
{
    struct mp3_decode_ctx ctx;
    struct mad_decoder decoder;
    int result;

    memset(&ctx, 0, sizeof(ctx));
    ctx.start = start;
    ctx.length = length;
    ctx.dev = dev;

    mad_decoder_init(&decoder, &ctx,
                     mp3_input, 0, 0, mp3_output,
                     mp3_error, 0);

    result = mad_decoder_run(&decoder, MAD_DECODER_MODE_SYNC);
    mad_decoder_finish(&decoder);

    if (ctx.write_error < 0)
        return ctx.write_error;

    return result;
}

static void audio_play_thread(void *data)
{
    (void)data;

    while (1) {
        thread_waiter_wait(&audio_res.waiter);
        mutex_lock(&audio_res.lock);

        if (!audio_res.buf) {
            audio_res.playing = 0;
            mutex_unlock(&audio_res.lock);
            continue;
        }

        if (audio_res.mode)
            decode_mp3_to_pcm(audio_res.dev, audio_res.buf, audio_res.size);
        else
            pcm_write_frame(audio_res.dev, audio_res.buf, audio_res.size / sizeof(short));

            free(audio_res.buf);
            audio_res.buf = NULL;
        audio_res.size = 0;
        audio_res.playing = 0;

        mutex_unlock(&audio_res.lock);
#ifdef HAL_AUDIO_SYNC
        thread_waiter_wakeup(&audio_res.waiter_complete);
#endif
    }
}

/**
 * @brief audio init
 *
 * @return @see IotReturnCode
 */
int32_t HAL_AudioInit(void)
{
    int ret;
    struct pcm_params params = {0};

    audio_res.dev = pcm_get("pwm-playback");
    if (!audio_res.dev) {
        printf("pwm audio: pwm-playback device not found\n");
        return ERR_CODE_TTS_RESOURCENOETFOUND;
    }

    params.channels = 1;
    params.pcm_data_fmt = pcm_fmt_S16LE;
    params.pcm_sample_rate = pcm_rate_48000;
    params.pcm_interface = pcm_interface_modeB;

    ret = pcm_enable(audio_res.dev, &params);
    if (ret < 0) {
        printf("pwm audio: pcm_enable failed %d\n", ret);
        return ERR_CODE_TTS_RESOURCENOETFOUND;
    }

    mutex_init(&audio_res.lock);
    thread_waiter_init(&audio_res.waiter);
#ifdef HAL_AUDIO_SYNC
    thread_waiter_init(&audio_res.waiter_complete);
#endif
    thread_create("audio play thread", 8192, audio_play_thread, NULL);

    // Log_d("audio init ok");
    return ERR_CODE_SUCCESS;
}

/**
 * @brief play mp3 file
 *
 * @param[in] mp3_file mp3 file name
 * @return @see IotReturnCode
 */
int32_t HAL_AudioVoiceSet(uint32_t voice_level)
{
    if (!audio_res.dev)
        return ERR_CODE_TTS_RESOURCENOETFOUND;

    if (pcm_set_volume(audio_res.dev, voice_level))
        return ERR_CODE_TTS_RESOURCENOETFOUND;

    return ERR_CODE_SUCCESS;
}

/**
 * @brief play mp3 file
 *
 * @param[in] mp3_file mp3 file name
 * @return @see IotReturnCode
 */
int32_t HAL_AudioVoiceGet(uint32_t *voice_level)
{
    if (!audio_res.dev)
        return ERR_CODE_TTS_RESOURCENOETFOUND;

    *voice_level = pcm_get_volume(audio_res.dev);
    return ERR_CODE_SUCCESS;
}

/**
 * @brief play mp3 file
 *
 * @param[in] mp3_file mp3 file name
 * @return @see IotReturnCode
 */
int32_t HAL_AudioMp3FilePlay(const char *mp3_file)
{
    int ret;
    int fd;
    int size;
    void *buf;
    ssize_t read_len;

    if (!audio_res.dev)
        return ERR_CODE_TTS_RESOURCENOETFOUND;

    if (audio_res.playing)
        return ERR_CODE_TTS_PLAYLISTFULL;

    mutex_lock(&audio_res.lock);

    ret = pcm_start(audio_res.dev);
    if (ret < 0) {
        mutex_unlock(&audio_res.lock);
        printf("pwm audio: pcm_start failed %d\n", ret);
        return ERR_CODE_TTS_RESOURCENOETFOUND;
    }

    fd = open(mp3_file, O_RDONLY, 0);
    if (fd < 0) {
        mutex_unlock(&audio_res.lock);
        return ERR_CODE_TTS_RESOURCENOETFOUND;
    }

    size = lseek(fd, 0, SEEK_END);
    if (size <= 0) {
        close(fd);
        mutex_unlock(&audio_res.lock);
        return ERR_CODE_TTS_FORMATERR;
    }
    lseek(fd, 0, SEEK_SET);

    buf = malloc(size);
    if (!buf) {
        close(fd);
        mutex_unlock(&audio_res.lock);
        return ERR_CODE_OS_NOMEM;
    }

    read_len = read(fd, buf, size);

    close(fd);

    if (read_len != size) {
        free(buf);
        mutex_unlock(&audio_res.lock);
        return ERR_CODE_TTS_PLAYFAIL;
    }

    audio_res.playing = 1;
    audio_res.buf = buf;
    audio_res.mode = 1;
    audio_res.size = size;
    mutex_unlock(&audio_res.lock);

    thread_waiter_wakeup(&audio_res.waiter);

#ifdef HAL_AUDIO_SYNC
    thread_waiter_wait(&audio_res.waiter_complete);
#endif
    return ERR_CODE_SUCCESS;
}

/**
 * @brief play pcm
 *
 * @param[in] pcm_data pcm name
 * @param[in] data_len pam data length
 * @return @see IotReturnCode
 */
int32_t HAL_AudioPcmPlay(void *pcm_data, uint32_t data_len)
{
    if (!audio_res.dev)
        return ERR_CODE_TTS_RESOURCENOETFOUND;

    if (!pcm_data || data_len == 0)
        return ERR_CODE_TTS_FORMATERR;

    if (audio_res.playing)
        return ERR_CODE_TTS_PLAYLISTFULL;

    mutex_lock(&audio_res.lock);

    int ret = pcm_start(audio_res.dev);
    if (ret < 0) {
        mutex_unlock(&audio_res.lock);
        printf("pwm audio: pcm_start failed %d\n", ret);
        return ERR_CODE_TTS_RESOURCENOETFOUND;
    }

    audio_res.buf = malloc(data_len);
    if (!audio_res.buf) {
        mutex_unlock(&audio_res.lock);
        return ERR_CODE_TTS_BUFNOTENOUGH;
    }

    memcpy(audio_res.buf, pcm_data, data_len);
    audio_res.playing = 1;
    audio_res.mode = 0;
    audio_res.size = data_len;

    mutex_unlock(&audio_res.lock);

    thread_waiter_wakeup(&audio_res.waiter);
#ifdef HAL_AUDIO_SYNC
    thread_waiter_wait(&audio_res.waiter_complete);
#endif
    return ERR_CODE_SUCCESS;
}

/**
 * @brief get audio play status
 *
 * @param[out] play_status audio play status 0 : idle  1 : playing
 * @return @see IotReturnCode
 */
int32_t HAL_AudioPlayStatusGet(int32_t *play_status)
{
    *play_status = audio_res.playing;
    return ERR_CODE_SUCCESS;
}

/**
 * @brief audio tts file path generate
 *
 * @param[out] dst_file_path dst tts file path
 * @param[in] dst_file_path_len dst tts file path buf size
 * @param[in] src_file_path file directory
 * @param[in] play_lang tts play language : mandarin or cantonese
 * @param[in] tts_type tts type : gn or yw
 * @return @see IotReturnCode
 */
void HAL_AudioTtsFilePathGenerate(char *dst_file_path, uint32_t dst_file_path_len, const char *src_file_path, const char *play_lang, const char *tts_type)
{
    uint32_t format_len;

    HAL_Snprintf((char *)dst_file_path, dst_file_path_len, &format_len, "%s/tts_%s_%s.pos", src_file_path, play_lang, tts_type);
}
/**************************************************************************************
 * resource operations
 **************************************************************************************/
/**
 * @brief set app firmware preload
 *
 * @param[in] file_name file name
 * @param[in] file_len file lenght
 * @return @see IotReturnCode
 */
int32_t HAL_AppFirmwarePreload(const char *file_name, uint32_t file_len)
{
    Log_d("HAL_AppFirmwarePreload %s len : %d", file_name, file_len);
    return ERR_CODE_SUCCESS;
}

/**
 * @brief get app firmware ver
 *
 */
int8_t *HAL_AppFirmwareVerGet(void)
{
    return IOT_DEVICE_APP_VERSION;
}

/**
 * @brief set sys firmware preload
 *
 * @param[in] filename file name
 * @param[in] total_len file lenght
 * @return @see IotReturnCode
 */
int32_t HAL_SysFirmwarePreload(const char *file_name, uint32_t file_len)
{
    Log_d("HAL_SysFirmwarePreload %s len : %d", file_name, file_len);
    return ERR_CODE_SUCCESS;
}

/**
 * @brief get sys firmware version
 *
 */
int8_t *HAL_SysFirmwareVerGet(void)
{
    static char sys_firmware_ver[64];

    memset(sys_firmware_ver, 0, sizeof(sys_firmware_ver));
    strcpy(sys_firmware_ver, "sys_firmware_ver_v1.0.0");
    return sys_firmware_ver;
}

/**************************************************************************************
 * keyboard operations
 **************************************************************************************/
/**
 * @brief pop one key value
 *
 * @param[out] key_value key value
 * @param[out] is_long_press whether key is long press
 * @return @see IotReturnCode
 */
int32_t HAL_KeyboardPop(uint32_t *key_value, uint32_t *is_long_press)
{
    *key_value = *is_long_press = 0;

    *key_value |= KEYBOARD_VALUE_POWER;
    return ERR_CODE_SUCCESS;
}

/**
 * @brief register keyboard callback
 *
 * @param[in] param callback function param
 * @param[in] callback call back function
 * @return @see IotReturnCode
 */
int32_t HAL_KeyboardRegister(void *param, void *callback)
{
    return ERR_CODE_SUCCESS;
}

/**************************************************************************************
 * battery charge operations
 **************************************************************************************/
/**
 * @brief register battery charge callback
 *
 * @param[in] param
 * @param[in] callback call back function
 * @return @see IotReturnCode
 */
int32_t HAL_BatteryChargeRegister(void *param, void *callback)
{
    return ERR_CODE_SUCCESS;
}

/**
 * @brief get signal level(csq, from 0 to 31)
 * @param [out] signal_level csq value
 * @return @see IotReturnCode
 */
int32_t HAL_ChargeStatusGet(ChargeStatus *charge_status)
{
    *charge_status = CHARGE_STATUS_UNCHARGE;
    return ERR_CODE_SUCCESS;
}

/**************************************************************************************
 * led operations
 **************************************************************************************/
void HAL_LedStatusSet(LedStatus status)
{

}

/**************************************************************************************
 * 旧固件静默重启标记: 因为老的固件静默重启由厂商实现，当从老的固件升级到新的固件后需要处理静默重启兼容逻辑
 **************************************************************************************/
/**
 * @brief get old firmware silent reboot flag
 *
 * @return @see 1 : silent reboot. 0 : normal reboot
 */
uint8_t HAL_OldFirmwareSilentRbootFlagGet(void)
{
    return 1;
}

/**
 * @brief clear old firmware silent reboot flag
 *
 * @return @see 1 : silent reboot. others : normal reboot
 */
void HAL_OldFirmwareSilentRbootFlagClear(void)
{

}

/**************************************************************************************
 * 厂商可能需要进入特殊模式用来进行测试而不走后面的联网逻辑
 **************************************************************************************/
int32_t HAL_FactoryModeCheck(uint8_t *factory_mode)
{
    *factory_mode = 0;
    return ERR_CODE_SUCCESS;
}

/**************************************************************************************
 * 厂商可能有特殊的获取三元组的逻辑,可放在这里单独实现
 **************************************************************************************/
int32_t HAL_FactorySpecialDeviceInfoGet(DeviceInfo *device_info)
{
    return ERR_CODE_SUCCESS;
}

/**************************************************************************************
 * 厂商可能有特殊的参数文件加载逻辑,可放在这里单独实现
 **************************************************************************************/
int32_t HAL_AppParamSpecialInfoGet(uint32_t *keep_alive, uint32_t *report_ivl, uint32_t *ota_type, uint32_t *monitor_switch, uint32_t *link_channel, uint32_t *main_link, uint8_t *play_lang)
{
    return ERR_CODE_SUCCESS;
}

int32_t HAL_AppParamSpecialInfoSet(uint32_t keep_alive, uint32_t report_ivl, uint32_t ota_type, uint32_t monitor_switch, uint32_t link_channel, uint32_t main_link, uint8_t *play_lang)
{
    return ERR_CODE_SUCCESS;
}

int32_t HAL_AdvParamSpecialInfoGet(uint32_t *ad_switch, uint32_t *ad_plan, uint32_t *ad_ivl_pay, uint32_t *ad_ivl_second, uint32_t *ad_start_time, uint32_t *ad_end_time, char *ad_file)
{
    return ERR_CODE_SUCCESS;
}

int32_t HAL_AdvParamSpecialInfoSet(uint32_t ad_switch, uint32_t ad_plan, uint32_t ad_ivl_pay, uint32_t ad_ivl_second, uint32_t ad_start_time, uint32_t ad_end_time, char *ad_file)
{
    return ERR_CODE_SUCCESS;
}

int32_t HAL_ServerIpParamSpecialInfoGet(char *srv_ip)
{
    return ERR_CODE_SUCCESS;
}

int32_t HAL_ServerIpParamSpecialInfoSet(char *srv_ip)
{
    return ERR_CODE_SUCCESS;
}

int32_t HAL_HttpdnsIpParamSpecialInfoGet(char *httpdns_ip)
{
    return ERR_CODE_SUCCESS;
}

int32_t HAL_HttpdnsIpParamSpecialInfoSet(char *httpdns_ip)
{
    return ERR_CODE_SUCCESS;
}

int32_t HAL_CPUIDGet(char *dst_buf, uint32_t buf_len)
{
    if (!dst_buf || !buf_len)
        return ERR_CODE_INVALIDPARAM;

    unsigned char chip_id_buff[CHIP_ID_SIZE];
    efuse_read_segment(CHIP_ID, chip_id_buff, CHIP_ID_SIZE);

    buf_len = CHIP_ID_SIZE > buf_len ? buf_len : CHIP_ID_SIZE;
    for (int i = 0; i < buf_len; i++)
        dst_buf[i] = (char)chip_id_buff[i];

    return ERR_CODE_SUCCESS;
}

int32_t HAL_FlashIDGet(char *dst_buf, uint32_t buf_len)
{
    if (!dst_buf || !buf_len)
        return ERR_CODE_INVALIDPARAM;

    struct storage_info *info = NULL;

#if defined(CONFIG_SFC_NAND)
    info = sfc_nand_flash_info();
#elif defined(CONFIG_SFC_NOR)
    info = sfc_nor_flash_info();
#elif defined(CONFIG_EMMC_DEVICE)
    info = mmc_device_storage_info();
#else
    info = NULL;
#endif

    if (!info) {
        Log_e("get flash ID err");
        return ERR_CODE_GENERALFAIL;
    }

    int len = snprintf(dst_buf, buf_len, "%lu", (unsigned long)info->id);
    if (len < 0) {
        Log_e("cpoy falsh id err");
        return ERR_CODE_GENERALFAIL;
    }

    if ((uint32_t)len >= buf_len) {
        dst_buf[buf_len - 1] = '\0';
        Log_d("dst_buf no enough(buf_len = %d)(flash id need len = %d),Only extract the preceding flash id", buf_len, len+1);
    }

    return ERR_CODE_SUCCESS;
}

/**************************************************************************************
 * camera/sensor operations
 **************************************************************************************/
#include <math.h>
#include <driver/camera.h>
#include <lib/image_scaler.h>

#define CAMERA_FRAME_DST_WIDTH      320
#define CAMERA_FRAME_DST_HEIGHT     240
#define CENTER_IMAGE_BUFSIZE_BASED  0

static struct camera_device *camera = NULL;
static struct camera_info *camera_info = NULL;

int32_t HAL_CameraSensorIDGet(char *dst_buf, uint32_t buf_len)
{
    if (!camera || !camera_info)
        return ERR_CODE_GENERALFAIL;

    if (!dst_buf || buf_len < 2)
        return ERR_CODE_INVALIDPARAM;

    unsigned char *sensor_ids = camera_get_sensor_id(camera);
    dst_buf[0] = sensor_ids[0];
    dst_buf[1] = sensor_ids[1];

    Log_d("CameraSensorID: 0x%x 0x%x", sensor_ids[0], sensor_ids[1]);
    return ERR_CODE_SUCCESS;
}

int32_t HAL_CameraTestpartnerGet(char *dst_buf, uint32_t buf_len)
{
    return ERR_CODE_SUCCESS;
}

int32_t HAL_CameraBLCGet(char *dst_buf, uint32_t buf_len)
{
    strcpy(dst_buf, "BLCBLCBLC");
    return ERR_CODE_SUCCESS;
}

int32_t HAL_CameraOpen(void)
{
    if (camera != NULL)
        return ERR_CODE_SUCCESS;

    struct camera_device *dev;
    struct camera_info *info;
    int ret;

    dev = camera_detect(0);
    if (!dev) {
        Log_e("camera not found");
        return ERR_CODE_GENERALFAIL;
    }

    info = camera_get_info(dev);
    if (!info) {
        Log_e("camera info not found");
        return ERR_CODE_GENERALFAIL;
    }

    ret = camera_power_on(dev);
    if (ret < 0) {
        Log_e("camera failed to power on");
        return ERR_CODE_GENERALFAIL;
    }

    ret = camera_stream_on(dev);
    if (ret < 0) {
        Log_e("camera failed to stream on");
        camera_power_off(dev);
        return ERR_CODE_GENERALFAIL;
    }

    Log_d("camera open %s (%dx%d)", info->name, info->width, info->height);
    camera = dev;
    camera_info = info;
    return ERR_CODE_SUCCESS;
}

int32_t HAL_CameraClose(void)
{
    if (camera != NULL) {
        camera_stream_off(camera);
        camera_power_off(camera);
        camera = NULL;
        camera_info = NULL;
    }

    Log_d("camera closed");
    return ERR_CODE_SUCCESS;
}

int32_t HAL_SendAuthCodeToPos(const char *authcode)
{
    return ERR_CODE_SUCCESS;
}

// 注册扫码成功回调函数
int32_t HAL_CameraDecodeRegister(camera_decode_success_cb_t callback)
{
    return ERR_CODE_SUCCESS;
}

uint32_t HAL_CameraGrayscaleImageSizeGet(void)
{
  return sizeof(PICTURE_DATA) - 1;
}

#if CENTER_IMAGE_BUFSIZE_BASED
/*
 * @brief 依据缓冲区大小计算最大输出图像的宽高
 *
 * @param dst_size 存储图像的缓冲区大小
 * @param bytes_per_pixel 每个像素字节数
 * @param pic_w 原始图像宽度
 * @param pic_h 原始图像高度
 * @param buf_w 缓冲区存储图像宽度指针
 * @param buf_h 缓冲区存储图像高度指针
 * @return int >0: 缓冲区存储图像字节数, -1: 失败
 */
static int image_center_size(uint32_t dst_size, double bytes_per_pixel,
                             uint32_t pic_w, uint32_t pic_h,
                             uint32_t *buf_w, uint32_t *buf_h)
{
    double buf_pixels = dst_size / bytes_per_pixel;
    double pic_pixels = pic_w * pic_h;
    if (buf_pixels < pic_pixels) return -1;

    double k = sqrt(buf_pixels / pic_pixels);
    double ideal_w = k * pic_w;
    double ideal_h = k * pic_h;

    while (ideal_w * ideal_h > buf_pixels) {
        if (ideal_w > ideal_h) ideal_w--; else ideal_h--;
    }

    if (ideal_w < pic_w || ideal_h < pic_h) return -1;

    *buf_w = (uint32_t)ideal_w & ~1;
    *buf_h = (uint32_t)ideal_h & ~1;

    return (*buf_h * *buf_w * bytes_per_pixel);
}
#endif

int32_t HAL_CameraCaptureGrayscaleImage(uint32_t test_mode, uint8_t *pic_buf, uint32_t pic_buf_size)
{
    if (!camera || !camera_info)
        return ERR_CODE_GENERALFAIL;

    if (!pic_buf || pic_buf_size <= 0)
        return ERR_CODE_INVALIDPARAM;

    uint32_t src_w = camera_info->width;
    uint32_t src_h = camera_info->height;
    uint32_t src_size = camera_info->frame_size;
    double bytes_per_pixel = (double)src_size / (src_w * src_h);

    uint32_t dst_w = CAMERA_FRAME_DST_WIDTH;
    uint32_t dst_h = CAMERA_FRAME_DST_HEIGHT;
    uint32_t out_w = dst_w, out_h = dst_h;
    int out_size = dst_w * dst_h * bytes_per_pixel;

    double zoom_w = (double)dst_w / src_w;
    double zoom_h = (double)dst_h / src_h;

    /* check dst width and height */
    if ((camera_info->data_fmt == CAMERA_PIX_FMT_YUYV ||
         camera_info->data_fmt == CAMERA_PIX_FMT_NV12) &&
        (dst_w % 2 != 0 || dst_h % 2 != 0)) {
        Log_e("width must be even if YUYV or NV12");
        return ERR_CODE_INVALIDPARAM;
    }

    /* check pic buf size */
    if (pic_buf_size < dst_w * dst_h * bytes_per_pixel) {
        Log_e("pic buf size not enough");
        return ERR_CODE_INVALIDPARAM;
    }

    /* skip old frames */
    unsigned int avail_frames = camera_get_available_frame_count(camera);
    if (avail_frames > 1)
        camera_skip_frames(camera, avail_frames - 1);

    /* get the latest frame */
    void *buf = camera_wait_frame(camera);
    if (!buf) {
        Log_e("camera failed to get frame, ret = %d", camera_get_frame_error(camera));
        return ERR_CODE_GENERALFAIL;
    }

    memset(pic_buf, 0, pic_buf_size);

#if CENTER_IMAGE_BUFSIZE_BASED
    /* center-aligned display params */
    out_size = image_center_size(pic_buf_size, bytes_per_pixel,
                            dst_w, dst_h, &out_w, &out_h);
    if (out_size < 0) {
        camera_put_frame(camera, buf);
        return out_size;
    }
    Log_d("calculate out image: %dx%d, out_size: %d", out_w, out_h, out_size);
#endif

    uint32_t off_x = (out_w - dst_w) / 2;
    uint32_t off_y = (out_h - dst_h) / 2;

    /* frame scaling */
    void *src, *dst;
    uint32_t unit_size, out_line;

    switch (camera_info->data_fmt)
    {
    case CAMERA_PIX_FMT_GREY:
        out_line = out_w;
        unit_size = 1;
        src = buf;
        dst = pic_buf + off_y * out_line + off_x;
        image_scaling_simple(src, dst, zoom_w, zoom_h,
                             dst_w, dst_h, unit_size, out_line);
        break;
    case CAMERA_PIX_FMT_YUYV:
        off_x = ALIGN(off_x, 2);
        off_y = ALIGN(off_y, 2);
        out_line = out_w * 2;
        unit_size = 4;
        src = buf;
        dst = pic_buf + off_y * out_line + off_x * 2;
        image_scaling_simple(src, dst, zoom_w, zoom_h,
                             dst_w / 2, dst_h, unit_size, out_line);
        break;
    case CAMERA_PIX_FMT_NV12:
        off_x = ALIGN(off_x, 2);
        off_y = ALIGN(off_y, 2);
        out_line = out_w;

        /* y plane */
        unit_size = 1;
        src = buf;
        dst = pic_buf + off_y * out_line + off_x;
        image_scaling_simple(src, dst, zoom_w, zoom_h,
                             dst_w, dst_h, unit_size, out_line);

        /* uv plane */
        unit_size = 2;
        src = (uint8_t *)buf + src_w * src_h;
        dst = pic_buf + out_w * out_h + (off_y / 2) * out_line + off_x;
        image_scaling_simple(src, dst, zoom_w, zoom_h,
                             dst_w / 2, dst_h / 2, unit_size, out_line);
        break;
    default:
        out_size = ERR_CODE_GENERALFAIL;
        break;
    }

    camera_put_frame(camera, buf);

    return out_size;
}

int32_t HAL_CameraOperatingNormally(uint8_t *flag)
{
    return ERR_CODE_SUCCESS;
}

/**************************************************************************************
 * uart operations
 **************************************************************************************/
#include <os/thread.h>
#include <os/thread_waiter.h>
#include <list.h>
#include <ring_mem.h>
#include <driver/uart.h>

#define UART_TX_ONCE_SIZE       (256)
#define UART_TX_BUF_SIZE        (8 * 1024)
#define UART_TIMEOUT_MS         1

#define UART_CFG_ID             1
#define UART_CFG_BAUD_RATE      3000000

// #define HAL_UART_TX_ASYNC       1

static struct uart_config config = {
    .uart_id            = UART_CFG_ID,
    .data_bits          = 8,
    .stop_bits          = 1,
    .loop_mode          = 0,
    .tx_poll_mode       = 1,
    .rx_poll_mode       = 0,
    .parity             = UART_PARITY_NONE,
    .follow_contrl      = UART_FC_NONE,
    .baud_rate          = UART_CFG_BAUD_RATE,
};

#ifdef HAL_UART_TX_ASYNC
typedef struct uart_params {
    char buf[UART_TX_BUF_SIZE];
    struct ring_mem ring;
    thread_waiter_t free_wait;
    thread_waiter_t used_wait;
    thread_ptr_t thread;
} uart_params_t;

static volatile int uart_running = 0;
static uart_params_t uart_tx;

static void uart_tx_init(void);
static void uart_tx_deinit(void);
#endif

int32_t HAL_UartOpen(void)
{
#ifdef HAL_UART_TX_ASYNC
    uart_tx_init();
#endif
    uart_start(&config);

    return ERR_CODE_SUCCESS;
}

int32_t HAL_UartClose(void)
{
#ifdef HAL_UART_TX_ASYNC
    uart_tx_deinit();
#endif
    uart_stop(&config);

    return ERR_CODE_SUCCESS;
}

int32_t HAL_UartRead(void *buf, uint32_t buf_size)
{
    int len = uart_receive_timeout(&config, buf, buf_size, UART_TIMEOUT_MS);
    if (len > 0)
        return len;

    return ERR_CODE_GENERALFAIL;
}

#ifdef HAL_UART_TX_ASYNC

static void uart_tx_thread(void *arg)
{
    char tmp[UART_TX_ONCE_SIZE];
    int avail_size, len;

    while (uart_running) {
        avail_size = ring_mem_readable_size(&uart_tx.ring);
        if (!avail_size) {
            thread_waiter_wait(&uart_tx.used_wait);
            continue;
        }

        len = (avail_size > UART_TX_ONCE_SIZE) ? UART_TX_ONCE_SIZE : avail_size;
        len = ring_mem_read(&uart_tx.ring, tmp, len);
        thread_waiter_wakeup(&uart_tx.free_wait);

        uart_send_timeout(&config, tmp, len, -1);
    }
}

static void uart_tx_init(void)
{
    memset(uart_tx.buf, 0, sizeof(uart_tx.buf));
    ring_mem_init(&uart_tx.ring, uart_tx.buf, sizeof(uart_tx.buf));

    thread_waiter_init(&uart_tx.free_wait);
    thread_waiter_init(&uart_tx.used_wait);

    uart_running = 1;
    uart_tx.thread = thread_create("uart_tx_thread", 4096, uart_tx_thread, NULL);
    assert(uart_tx.thread);
}

static void uart_tx_deinit(void)
{
    uart_running = 0;
    thread_waiter_wakeup(&uart_tx.used_wait);
    thread_join(uart_tx.thread, NULL);

    ring_mem_clean(&uart_tx.ring);
}

int32_t HAL_UartWrite(const void *data, uint32_t data_len)
{
    if (!uart_running)
        return ERR_CODE_GENERALFAIL;

    char *ptr = (char *)data;
    int left = data_len;
    int free_size, len;

    while (left > 0) {
        free_size = ring_mem_writable_size(&uart_tx.ring);
        if (!free_size) {
            thread_waiter_wait(&uart_tx.free_wait);
            continue;
        }

        len = left > free_size ? free_size : left;
        len = ring_mem_write(&uart_tx.ring, ptr, len);
        thread_waiter_wakeup(&uart_tx.used_wait);

        left -= len;
        ptr += len;
    }

    return data_len - left;
}

int32_t uart_tx_is_finished(void)
{
    return !(ring_mem_readable_size(&uart_tx.ring) > 0);
}

#else

int32_t HAL_UartWrite(const void *data, uint32_t data_len)
{
    int len = uart_send_timeout(&config, data, data_len, UART_TIMEOUT_MS);
    if (len > 0)
        return len;

    return ERR_CODE_UART_WRITE_TIMEOUT;
}

#endif

int32_t HAL_UartReadRegister(uart_read_cb_t callback)
{
    return ERR_CODE_SUCCESS;
}
