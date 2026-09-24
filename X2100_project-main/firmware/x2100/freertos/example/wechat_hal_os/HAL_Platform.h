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
 * @file HAL_Platform.h
 * @brief hal platform interface
 * @author albertai (albertai@tencent.com)
 * @version 1.0
 * @date 2024-11-13
 *
 * @par Change Log:
 */

#ifndef SPEAKER_SDK_HAL_PLATFORM_H_
#define SPEAKER_SDK_HAL_PLATFORM_H_

#if defined(__cplusplus)
extern "C" {
#endif

#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "mbedtls/ssl.h"

#define IOT_DEVICE_APP_VERSION "app_wxpay_linux_v1.0.0"

#define __PLATFORM_WXPAY_VARIABLE_DEFINE        // 该编译宏表示变量类型的定义。厂商根据自己的平台设置基础变量的类型,也可以通过一个采用包含自己头文件的形式来实现
#define __PLATFORM_WXPAY_APP_ENTRY              // 该编译宏表示app入口函数的名字, 音箱sdk基于linux环境调试,入口为main。其他平台为 wxpay_main_thread_init，厂商需要将其自己的入口函数调整为此函数
#define __PLATFORM_WXPAY_STACK_STATIC_ARRAY     // 该编译宏表示线程栈的地址, 默认指定为静态的数组，如果厂商平台会自行动态分配栈地址的话可以屏蔽该宏
#define __PLATFORM_WXPAY_CONFIG_FILE_PATH       // 该编译宏表示各文件的路径，当前为linux环境。厂商可以根据自己平台环境做调整
// 厂商可能有特殊的获取三元组的逻辑，到时打开该宏定义. 词为博实结特定兼容逻辑,后续新的型号不允许再出现这种情况
//#define __PLATFORM_WXPAY_SPECIAL_DEVICE_INFO
// 部分厂商参数文件(app参数文件、广告参数文件、httpdns ip参数文件、server ip参数文件)没有采用sdk逻辑, 额外封装了对应的函数。厂商需要开启该宏然后单独适配对应的函数. 此为博实结特定兼容逻辑,后续新的型号不允许再出现这种情况
//#define __PLATFORM_WXPAY_SPECIAL_CONFIG_INFO
// 博实结TX01早期做了半双链路,IoTDA链路采用新的物模型action播报协议,需要兼容该协议逻辑，仅博实结F1 TX02_V2版本才需要打开该宏
//#define __PLATFORM_WXPAY_TEMPLATE_ACTION_PROTOCOL

// 为了方便测试可以选择开启如下宏定义，设备则连接到腾讯云(IOTHUB)。注意连接腾讯云只支持一条主链路
//#define __PLATFORM_WXPAY_IOTHUB_CONNECT

// 宏开关设置连接iotda通道时是测试环境还是正式环境
#define __PLATFORM_WXPAY_IOTDA_CONNECT_TEST
// IoTDA 通道相关参数
#ifdef __PLATFORM_WXPAY_IOTDA_CONNECT_TEST
#define IOT_MQTT_DIRECT_DOMAIN_IOTDA    "paydevicetest.iot.wechatpay.cn"
#define IOT_MQTT_DIRECT_DOMAIN_IOTDA1   "paydevicetest.iot.wechatpay.cn"
#define IOT_MQTT_DIRECT_DOMAIN_IOTDA2   "paydevicetest.iot.wechatpay.cn"
#else
#define IOT_MQTT_DIRECT_DOMAIN_IOTDA    "paydevice.iot.wechatpay.cn"
#define IOT_MQTT_DIRECT_DOMAIN_IOTDA1   "paydevice1.iot.wechatpay.cn"
#define IOT_MQTT_DIRECT_DOMAIN_IOTDA2   "paydevice2.iot.wechatpay.cn"
#endif

// 是否使用randombuffer，正式版本要开启，测试环境可关掉后在代码中写死私钥进行测试
//#define USE_RANDOMBUFFER

// IOTHUB 通道相关参数
#define IOT_MQTT_DIRECT_DOMAIN_IOTHUB   "wx-mqtt.tencentdevices.com"
/* IoT C-SDK APPID */
#define IOT_DEVICE_SDK_APPID_IOTHUB     "21010406"

#ifdef __PLATFORM_WXPAY_VARIABLE_DEFINE
/* signed */
#ifndef int8_t
#define int8_t  char
#endif

#ifndef int16_t
#define int16_t short
#endif

#ifndef int32_t
#define int32_t int
#endif

#ifndef int64_t
#define int64_t long int
#endif

/* unsigned */
#ifndef uint8_t
#define uint8_t unsigned char
#endif

#ifndef uint16_t
#define uint16_t unsigned short
#endif

#ifndef uint32_t
#define uint32_t unsigned int
#endif

#ifndef uint64_t
#define uint64_t unsigned long int
#endif

/* bool */
#ifndef bool
#define bool int
#endif

/* NULL */
#ifndef NULL
#define NULL ((void*)0)
#endif
#endif

/**
 * @brief SPEAKER SDK return/error code.
 * Values less than 0 are specific error codes
 * Value of 0 is successful return
 * Values greater than 0 are specific non-error return codes
 *
 */
typedef enum {
    ERR_CODE_SUCCESS                            = 0,                /**< successful return*/
    ERR_CODE_GENERALFAIL                        = -1,               /**< general fail code*/
    ERR_CODE_INVALIDPARAM                       = -2,               /**< invalid param*/
    ERR_CODE_DATA_CORRUPTED                     = -3,               /**< data corrupted*/
    ERR_CODE_DATA_EXPIRED                       = -4,              /**< data expired */

    /*begin 4G return code*/
    ERR_CODE_4G_SIM_NOT_READY                   = -600,             /**< sim not ready */
    ERR_CODE_4G_BASESTATION_NOT_ATTACH          = -601,             /**< basestation not attach */
    ERR_CODE_4G_PDP_NOT_ACTIVED                 = -602,             /**< pdp not actived */
    /*end 4G return code*/

    /*begin json return code*/
    ERR_CODE_JSON_PARSE                         = -700,             /**< json parse error*/
    ERR_CODE_JSON_BUFFER_TRUNCATED              = -701,             /**< json buffer truncated*/
    ERR_CODE_JSON_BUFFER_NOT_ENOUGH             = -702,             /**< json buffer not enough*/
    ERR_CODE_JSON_GENERATE                      = -703,             /**< json generate error*/
    ERR_CODE_JSON_MAX_JSON_TOKEN                = -704,             /**< json token out of range*/
    ERR_CODE_JSON_MAX_APPENDING_REQUEST         = -705,             /**< json appending request out of range*/
    /*end json return code*/

    /*begin http return code*/
    ERR_CODE_HTTP_UNKNOWN                       = -800,             /**< http unknown error*/
    ERR_CODE_HTTP_CLOSED                        = -801,             /**< http server close the connection*/
    ERR_CODE_HTTP_PROTOCOL                      = -802,             /**< http protocol error*/
    ERR_CODE_HTTP_URL_PARSE                     = -803,             /**< http url parse fail*/
    ERR_CODE_HTTP_CONN                          = -804,             /**< http connect fail*/
    ERR_CODE_HTTP_AUTH                          = -805,             /**< http auth fail*/
    ERR_CODE_HTTP_NOT_FOUND                     = -806,             /**< http 404*/
    ERR_CODE_HTTP_TIMEOUT                       = -807,             /**< http timeout*/
    ERR_CODE_HTTP_BUFFER_NOT_ENOUTH             = -808,             /**< http buffer not enough*/
    /*end http return code*/

    /*begin mqtt return code*/
    ERR_CODE_MQTT_PUSH_TO_LIST_FAIL             = -900,             /**< fail to push node to mqtt waitting list*/
    ERR_CODE_MQTT_NO_CONN                       = -901,             /**< noet connected with mqtt server*/
    ERR_CODE_MQTT_UNKNOWN                       = -902,             /**< mqtt unknown error*/
    ERR_CODE_MQTT_ATTEMPTING_RECONN             = -903,             /**< reconnecting with mqtt server*/
    ERR_CODE_MQTT_MAX_REGISTERS                 = -904,             /**< mqtt topic register out of range*/
    ERR_CODE_MQTT_NOTHING_TO_READ               = -905,             /**< mqtt nothing to read*/
    ERR_CODE_MQTT_PACKET_READ                   = -906,             /**< something wrong when read mqtt packet*/
    ERR_CODE_MQTT_REQUEST_TIMEOUT               = -907,             /**< mqtt request timeout*/
    ERR_CODE_MQTT_CONNACK_UNKNOWN               = -908,             /**< mqtt connection refused : unknown error*/
    ERR_CODE_MQTT_UNACCEPTABLE_PROTOCOL_VERSION = -909,             /**< mqtt version invalid*/
    ERR_CODE_MQTT_CONNACK_IDENTIFIER_REJECTED   = -910,             /**< mqtt identifier rejected*/
    ERR_CODE_MQTT_CONNACK_SERVER_UNAVAILABLE    = -911,             /**< mqtt service not available*/
    ERR_CODE_MQTT_CONNACK_BAD_USERDATA          = -912,             /**< mqtt bad user name or password*/
    ERR_CODE_MQTT_CONNACK_NOT_AUTHORIZED        = -913,             /**< mqtt not authorized*/
    ERR_CODE_MQTT_RX_MESSAGE_INVAL              = -914,             /**< received invalid msg*/
    ERR_CODE_MQTT_BUF_TOO_SHORT                 = -915,             /**< mqtt receive buff not enough*/
    ERR_CODE_MQTT_QOS_NOT_SUPPORT               = -916,             /**< QoS level not supported*/
    ERR_CODE_MQTT_MAX_TOPIC_LENGTH              = -917,             /**< topic length oversize*/
    ERR_CODE_MQTT_PROPERTY_EXIST                = -918,             /**< property already exist*/
    ERR_CODE_MQTT_PROPERTY_NOT_EXIST            = -919,             /**< property not exist*/
    ERR_CODE_MQTT_PROPERTY_SYNC_TIMEOUT         = -920,             /**< property sync timeout*/
    ERR_CODE_MQTT_PROPERTY_SYNC_REJECTED        = -921,             /**< property sync rejected*/
    ERR_CODE_MQTT_PROPERTY_REPORT_TIMEOUT       = -922,             /**< property report timeout*/
    ERR_CODE_MQTT_PROPERTY_REPORT_REJECTED      = -923,             /**< property report rejected*/
    ERR_CODE_MQTT_SUB                           = -924,             /**< mqtt subscribe fail*/
    ERR_CODE_MQTT_UNSUB                         = -925,             /**< mqtt unsubscribe fail*/
    ERR_CODE_MQTT_RECONNECT_TIMEOUT             = -926,             /**< mqtt reconnect timeout*/
    /*end mqtt return code*/

    /*begin os return code*/
    ERR_CODE_OS_NOTHREAD                        = -1000,            /**< thread count exceeding the limit*/
    ERR_CODE_OS_NOMEM                           = -1001,            /**< memory not enough*/
    ERR_CODE_OS_MUTEXCREATEFAIL                 = -1002,            /**< mutex create fail*/
    ERR_CODE_OS_MUTEXLOCKFAIL                   = -1003,            /**< mutex lock fail*/
    ERR_CODE_OS_SEMCREATEFAIL                   = -1004,            /**< sem create fail*/
    ERR_CODE_OS_SEMWAITTIMEOUT                  = -1005,            /**< sem wait timeout*/
    /*end os return code*/

    /*begin fs return code*/
    ERR_CODE_FS_ACCESS                          = -1100,            /**< permisssion error*/
    ERR_CODE_FS_BADFD                           = -1101,            /**< invalid fd*/
    ERR_CODE_FS_IO                              = -1102,            /**< IO error*/
    ERR_CODE_FS_DIR                             = -1103,            /**< directory not file name*/
    ERR_CODE_FS_NOFILE                          = -1104,            /**< file not exist*/
    ERR_CODE_FS_NOSPACE                         = -1105,            /**< file space not enough*/
    ERR_CODE_FS_WRITEFAIL                       = -1106,            /**< file write error*/
    ERR_CODE_FS_READFAIL                        = -1107,            /**< file read error*/
    ERR_CODE_FS_ENDFILE                         = -1108,            /**< reach the end of the file */
    ERR_CODE_FS_DIRNOTEXIST                     = -1109,            /**< directory not exist*/
    ERR_CODE_FS_DIREND                          = -1110,            /**< end of directory*/
    ERR_CODE_FS_OTHERS                          = -1111,            /**< other error */
    ERR_CODE_FS_FILEEXSIT                       = -1112,            /**< file exsits */
    /*end fs return code*/

    /*begin tcp return code*/
    ERR_CODE_TCP_ADDRNOTAVAIL                   = -1200,            /**< invalid addr*/
    ERR_CODE_TCP_BADFD                          = -1201,            /**< invalid tcp fd*/
    ERR_CODE_TCP_CONNRESUSED                    = -1202,            /**< connect is refused*/
    ERR_CODE_TCP_RESET                          = -1203,            /**< connect is reset*/
    ERR_CODE_TCP_PEERSHUTDOWN                   = -1204,            /**< connect is shutdown by peer*/
    ERR_CODE_TCP_FDNOTENOUGH                    = -1205,            /**< tcp fd is not enough*/
    ERR_CODE_TCP_NOBUF                          = -1206,            /**< tcp buffer is not enough*/
    ERR_CODE_TCP_NETDOWN                        = -1207,            /**< link is down*/
    ERR_CODE_TCP_CONNECTFAIL                    = -1208,            /**< connect fail*/
    ERR_CODE_TCP_READTIMEOUT                    = -1209,            /**< tcp read timeout*/
    ERR_CODE_TCP_WRITETIMEOUT                   = -1210,            /**< tcp write timeout*/
    ERR_CODE_TCP_READFAIL                       = -1211,            /**< tcp read fail*/
    ERR_CODE_TCP_WRITEFAIL                      = -1212,            /**< tcp write fail*/
    ERR_CODE_TCP_NOTHINGTOREAD                  = -1213,            /**< tcp nothing to read*/
    ERR_CODE_TCP_DNSTIMEOUT                     = -1214,            /**< dns resolve timeout*/
    ERR_CODE_TCP_DNSFAIL                        = -1215,            /**< dns resolve fail*/
    /*end tcp return code*/

    /*begin tls return code*/
    ERR_CODE_TLS_INIT                           = -1300,            /**< tls init fail*/
    ERR_CODE_TLS_CRTINVALID                     = -1301,            /**< tls ca parse fail*/
    ERR_CODE_TLS_CRTVERIFY                      = -1302,            /**< tls crt verify fail*/
    ERR_CODE_TLS_HANDSHAKEFAIL                  = -1303,            /**< tls handshake fail*/
    ERR_CODE_TLS_READTIMEOUT                    = -1304,            /**< tls read timeout*/
    ERR_CODE_TLS_WRITETIMEOUT                   = -1305,            /**< tls write timeout*/
    ERR_CODE_TLS_CONNTIMEOUT                    = -1306,            /**< tls connect timeout*/
    ERR_CODE_TLS_READFAIL                       = -1307,            /**< tls read fail*/
    ERR_CODE_TLS_WRITEFAIL                      = -1308,            /**< tls write fail*/
    ERR_CODE_TLS_NOTHINGTOREAD                  = -1309,            /**< tls nothing to read*/
    ERR_CODE_TLS_PEERSHUTDOWN                   = -1310,            /**< connect is shutdown by peer*/
    /*end tls return code*/

    /*begin audio return code*/
    ERR_CODE_TTS_PARTOUTPUT                     = -1400,            /**< tts part generate fail*/
    ERR_CODE_TTS_NONEOUTPUT                     = -1401,            /**< tts full generate fail*/
    ERR_CODE_TTS_RESOURCENOETFOUND              = -1402,            /**< tts resource not found*/
    ERR_CODE_TTS_BUFNOTENOUGH                   = -1403,            /**< tts buf not enough*/
    ERR_CODE_TTS_INITFAIL                       = -1404,            /**< tts algorithm init fail*/
    ERR_CODE_TTS_PLAYLISTFULL                   = -1405,            /**< tts play list is full*/
    ERR_CODE_TTS_PLAYLISTEMPTY                  = -1406,            /**< tts play list is empty*/
    ERR_CODE_TTS_FORMATERR                      = -1407,            /**< tts data format error*/
    ERR_CODE_TTS_PLAYFAIL                       = -1408,            /**< tts play fail*/
    ERR_CODE_TTS_KEYINTERRUPT                   = -1409,            /**< tts play is interrupt by key press*/
    ERR_CODE_TTS_PLAYTIMEOUT                    = -1410,            /**< tts play timeout in queue ts - out queue ts > 60s*/
    /*end audio return code*/

    /*begin auth code report return code*/
    ERR_CODE_UART_WRITE_TIMEOUT                 = -1500,            /**< auth code queu full*/
    /*end auth code report return code*/

    /*begin factory mode return code*/
    ERR_CODE_FACTORY_FILE_CORRUPTED             = -1600,            /**< file corrupted*/
    ERR_CODE_FACTORY_DATA_CORRUPTED             = -1601,            /**< data corrupted*/
    /*end factory mode return code*/
} IotReturnCode;


/**
 * @brief key value
 *
 */
typedef enum {
    KEYBOARD_VALUE_POWER                             = 0x01 << 0,        /**< power key */
    KEYBOARD_VALUE_FUNC                              = 0x01 << 1,        /**< function key */
    KEYBOARD_VALUE_VOICEADD                          = 0x01 << 2,        /**< voice add key */
    KEYBOARD_VALUE_VOICEDEC                          = 0x01 << 3,        /**< voice dev key */
    KEYBOARD_VALUE_ORDERSUM                          = 0x01 << 4,        /**< order sum key */
    KEYBOARD_VALUE_ORDERLIST                         = 0x01 << 5,        /**< order list key */
    KEYBOARD_VALUE_PREORDER                          = 0x01 << 6,        /**< pre order key */
    KEYBOARD_VALUE_NEXTORDER                         = 0x01 << 7,        /**< next oder key */
} KeyboardValue;

/**************************************************************************************
 * begin os thread
 **************************************************************************************/
// 厂商根据自身平台调整各线程的栈空间和优先级

// 连接thread(最多3个)，优先级要求要高一些
// 1,处理4G，mqtt连接
// 2,处理keepalive
// 3,处理mqtt读和消息分发
#define HAL_ADAPT_MQTT_CONNECT_THREAD_STACK_SIZE      (16*1024)
#define HAL_ADAPT_MQTT_CONNECT_THREAD_PRIORITY        (4)         // higher

// 定时器thread，一般优先级即可
// 1,处理定时器事件,给定时器thread投递定时器到期事件
#define HAL_ADAPT_TIMER_THREAD_STACK_SIZE             (16*1024)
#define HAL_ADAPT_TIMER_THREAD_PRIORITY               (2)

// mqtt上行消息thread，优先级可以较低
// 1,处理所有mqtt topic上行消息
#define HAL_ADAPT_MQTT_PUBLISH_THREAD_STACK_SIZE      (16*1024)
#define HAL_ADAPT_MQTT_PUBLISH_THREAD_PRIORITY        (1)         // lowest

// 语音播报thread，优先级要求高一些，避免被其他线程抢占引发播报卡顿(如果厂商自己额外实现了语音播报逻辑避免被打断的话，可自行调整该线程优先级)
// 1,处理tts语音播报和广告文件播报
#define HAL_ADAPT_AUDIO_THREAD_STACK_SIZE             (16*1024)
#define HAL_ADAPT_AUDIO_THREAD_PRIORITY               (4)         // higher

// 资源/日志 thread，优先级可以调到较低
// 1,处理固件/资源的操作
// 2,处理日志的http上报
#define HAL_ADAPT_RESOURCE_OPT_THREAD_STACK_SIZE          (24*1024)
#define HAL_ADAPT_RESOURCE_OPT_THREAD_PRIORITY            (2)         // lowest

// 付款码上报thread，优先级可以低一些
// 1,处理付款码上报
#define HAL_ADAPT_AUTH_CODE_REPORT_THREAD_STACK_SIZE             (16*1024)
#define HAL_ADAPT_AUTH_CODE_REPORT_THREAD_PRIORITY               (2)

// main thread，优先级可以调到较低
// 1,处理联网影子同步
// 2,处理ntp时间同步
// 3,处理日志等级同步
// 4,开机上报全量影子信息
// 5,定时上报影子信息
// 6,同步httpdns ip 和 ip list
// 7,按键响应
// 8,处理led灯
// 9,处理重启
// 10,处理联网/断网语音提示
#define HAL_ADAPT_MAIN_THREAD_STACK_SIZE              (16*1024)
#define HAL_ADAPT_MAIN_THREAD_PRIORITY                (2)         // lowest

/**
 * @brief Theard entry function.
 *
 */
typedef void (*ThreadRunFunc)(void *arg);

/**
 * @brief Thread params to create.
 *
 */
typedef struct {
    char         *thread_name;              /**< thread name */
    uint64_t      thread_id;                /**< thread handle */
    ThreadRunFunc thread_func;              /**< thread entry function */
    void         *user_arg;                 /**< thread entry arg */
    uint16_t      priority;                 /**< thread priority */
    void         *stack_base;               /**< thread stack base */
    uint32_t      stack_size;               /**< thread stack size */
    void         *queue;                    /**< thread event queue */
} ThreadParams;

/**
 * @brief platform-dependent thread create function
 *
 * @param[in,out] params params to create thread @see ThreadParams
 * @return @see IotReturnCode
 */
int32_t HAL_ThreadCreate(ThreadParams *params);

/**
 * @brief platform-dependent get thread id
 *
 * @return @see IotReturnCode
 */
int32_t HAL_ThreadId(uint64_t *thread_id);

/**
 * @brief platform-dependent thread destroy function.
 *
 */
void HAL_ThreadDestroy(uint64_t thread_id);
/**************************************************************************************
 * end os thread
 **************************************************************************************/

/**************************************************************************************
 * begin os memory
 **************************************************************************************/
/**
 * @brief Malloc from heap.
 *
 * @param[in] size size to malloc
 * @param[out] ptr buf point to malloc
 * @return @see IotReturnCode
 */
int32_t HAL_Malloc(uint32_t size, void **ptr);

/**
 * @brief Free buffer malloced by HAL_Malloc.
 *
 * @param[in] ptr
 */
void HAL_Free(void *ptr);

/**
 * @brief Printf with format.
 *
 * @param[in] fmt format
 */
/**************************************************************************************
 * end os memory
 **************************************************************************************/

/**************************************************************************************
 * begin os format
 **************************************************************************************/
/**
 * @brief format and output to local uart/usb
 *
 * @param[in] fmt format
 * @return @see IotReturnCode
 */
void HAL_Printf(const char *fmt, ...);

/**
 * @brief Snprintf with format.
 *
 * @param[out] str buffer to save
 * @param[in] len buffer len
 * @param[in] fmt format
 * @return @see IotReturnCode
 */
int32_t HAL_Snprintf(char *str, const uint32_t str_len, uint32_t *dst_len, const char *fmt, ...);
/**************************************************************************************
 * end os format
 **************************************************************************************/

/**************************************************************************************
 * begin os system sleep
 **************************************************************************************/
/**
 * @brief Sleep for ms
 *
 * @param[in] ms ms to sleep
 */
void HAL_SleepMs(uint32_t ms);
/**************************************************************************************
 * end os system sleep
 **************************************************************************************/

/**************************************************************************************
 * begin os mutex and semaphore
 **************************************************************************************/
/**
 * @brief Mutex create.
 *
 * @param[out] mutex pointed to created mutex
 * @return @see IotReturnCode
 */
int32_t HAL_MutexCreate(void **mutex);

/**
 * @brief Mutex destroy.
 *
 * @param[in] mutex pointer to mutex
 * @return NULL
 */
void HAL_MutexDestroy(void *mutex);

/**
 * @brief Mutex lock.
 *
 * @param[in,out] mutex pointer to mutex
 */
void HAL_MutexLock(void *mutex);

/**
 * @brief Mutex try lock.
 *
 * @param[in] mutex pointer to mutex
 * @return @see IotReturnCode
 */
int32_t HAL_MutexTryLock(void *mutex);

/**
 * @brief Mutex unlock.
 *
 * @param[in,out] mutex pointer to mutex
 */
void HAL_MutexUnlock(void *mutex);


/**
 * @brief platform-dependent semaphore create function.
 *
 * @param[out] sem pointered to created sem
 * @return @see IotReturnCode
 */
int32_t HAL_SemaphoreCreate(void **sem);

/**
 * @brief platform-dependent semaphore destory function.
 *
 * @param[in] sem pointer to semaphore
 */
void HAL_SemaphoreDestroy(void *sem);

/**
 * @brief platform-dependent semaphore post function.
 *
 * @param[in] sem pointer to semaphore
 */
void HAL_SemaphorePost(void *sem);

/**
 * @brief platform-dependent semaphore wait function.
 *
 * @param[in] sem pointer to semaphore
 * @param[in] timeout_ms wait timeout
 * @return @see IotReturnCode
 */
int32_t HAL_SemaphoreWait(void *sem, uint32_t timeout_ms);
/**************************************************************************************
 * end os mutex and semaphore
 **************************************************************************************/

/**************************************************************************************
 * begin fs
 **************************************************************************************/
typedef enum {
    FS_OPEN_MODE_R = 0,
    FS_OPEN_MODE_RW,
} FsOpenMode;

typedef enum {
    FS_SEEK_BEGIN = 0,
    FS_SEEK_CUR,
    FS_SEEK_END,
} FsSeekWhence;

/**
 * @brief open file
 * @param[in] filename file path name
 * @param[in] mode file open mode see @FsOpenMode

 * @return @see IotReturnCode
 */
int32_t HAL_FileOpen(const char *file_name, FsOpenMode mode, uint64_t *fd);

/**
 * @brief file seek
 * @param[in] fd file fd
 * @param[in] offset fd offset
 * @param[in] whence @see FsSeekWhence

 * @return @see IotReturnCode
 */
int32_t HAL_FileSeek(uint64_t fd, uint32_t offset, FsSeekWhence whence);

/**
 * @brief close file
 * @param[in] filename file path name
 * @param[in] mode file open mode see @FsOpenMode

 * @return @see IotReturnCode
 */
int32_t HAL_FileClose(uint64_t fd);

/**
 * @brief Functions for saving file into NVS(files/FLASH)
 * @param[in] fd file fd
 * @param[in] buf source need write buffer
 * @param[in] write_len length of file to write
 * @param[out] written_len acturelly written len
 * @return @see IotReturnCode
 */
int32_t HAL_FileWrite(uint64_t fd, const void *buf, uint32_t write_len, uint32_t *written_len);

/**
 * @brief Functions for reading file from NVS(files/FLASH)
 * @param[in] fd file fd
 * @param[in] buf destination buffer to store readed data
 * @param[in] read_len length to read
 * @param[out] readed_len acturelly readed len
 * @return @see IotReturnCode
 */
int32_t HAL_FileRead(uint64_t fd, void *buf, uint32_t read_len, uint32_t *readed_len);

/**
 * @brief get file char value
 * @param[in] fd file fd

 * @return -1 for failed, others for char value
 */
int32_t HAL_FileGetChar(uint64_t fd);

/**
 * @brief Functions for deleting file in NVS(files/FLASH).
 * @param[in] file_name file path name
 * @return @see IotReturnCode
 */
int32_t HAL_FileDel(const char *file_name);

/**
 * @brief Functions for reading the size of file in NVS(files/FLASH).
 * @param[in] filename file path name
 * @return @see IotReturnCode
 */
int32_t HAL_FileGetSize(const char *file_name, uint32_t *file_size);

/**
 * @brief rename file
 * @param[in] src_path source file
 * @param[in] dst_path destination file

 * @return 0 for success, others for failure
 */
int32_t HAL_FileRename(const char *src_path, const char *dst_path);

/**
 * @brief open directory
 * @param[in] dir_path directory path name
 * @param[out] fd directory fd

 * @return @see IotReturnCode
 */
int32_t HAL_FileOpenDir(const char *dir_path, uint64_t *fd);

/**
 * @brief close directory
 * @param[in] dir_path directory path name
 * @param[out] fd directory fd

 * @return @see IotReturnCode
 */
int32_t HAL_FileCloseDir(uint64_t fd);

/**
 * @brief close directory
 * @param[in] dir_path directory path name
 * @param[out] fd directory fd

 * @return @see IotReturnCode
 */
int32_t HAL_FileReadDir(uint64_t fd, uint8_t *file_name, uint32_t file_name_len);


// 片内FLASH的操作
int32_t HAL_InternalFileSizeGet(const char *path, uint32_t *size);

int32_t HAL_InternalFileDel(const char *path);

/**
 * @brief Functions for saving file into NVS(files/FLASH)
 * @param[in] path file path
 * @param[in] data source need write buffer
 * @param[in] data_len length of file to write
 * @param[out] written_len acturelly written len
 * @return @see IotReturnCode
 */
int32_t HAL_InternalFileWrite(const char *path, const void *data, uint32_t data_len, uint32_t *write_len);

/**
 * @brief Functions for reading file from NVS(files/FLASH)
 * @param[in] buf destination buffer to store readed data
 * @param[in] read_len length to read
 * @param[out] readed_len acturelly readed len
 * @return @see IotReturnCode
 */
int32_t HAL_InternalFileRead(const char *path, void *buf, uint32_t read_len, uint32_t *readed_len);

/**************************************************************************************
 * end fs
 **************************************************************************************/

/**************************************************************************************
 * begin network tcp
 **************************************************************************************/
#define TCP_CONNECT_TIMEOUT_MS      (10*1000)
#define DNS_RESOLVE_TIMEOUT_MS      (10*1000)
/**
 * @brief TCP connect in linux
 *
 * @param[in] ip ip to conenct
 * @param[in] port port to connect
 * @param[in] timeout_ms connect timeout ms
 * @param[out] fd tcp socket fd
 * @return @see IotReturnCode
 */
int32_t HAL_TcpConnect(const char *ip, uint16_t port, uint32_t timeout_ms, uint64_t *fd);

/**
 * @brief TCP disconnect
 *
 * @param[in] fd socket fd
 * @return @see IotReturnCode
 */
int32_t HAL_TcpDisconnect(uint64_t fd);

/**
 * @brief TCP write
 *
 * @param[in] fd socket fd
 * @param[in] buf buf to write
 * @param[in] write_len want write len
 * @param[in] timeout_ms timeout
 * @param[out] written_len data written length
 * @return @see IotReturnCode
 */
int32_t HAL_TcpWrite(uint64_t fd, const void *buf, uint32_t write_len, uint32_t timeout_ms, uint32_t *written_len);

/**
 * @brief TCP read.
 *
 * @param[in] fd socket fd
 * @param[out] buf buffer to save read data
 * @param[in] read_len read buffer len
 * @param[in] timeout_ms timeout
 * @param[out] read_len length of data readed
 * @return @see IotReturnCode
 */
int32_t HAL_TcpRead(uint64_t fd, void *buf, uint32_t read_len, uint32_t timeout_ms, uint32_t *readed_len);

/**
 * @brief dns resolve.
 *
 * @param[in] domain domain name
 * @param[out] ip_list ip list of the given domain
 * @param[in] ip_list_len buf to save the ip list
 * @param[in] timeout_ms timeout
 * @return @see IotReturnCode
 */
int32_t HAL_DnsResolve(const char *domain, void *ip_list, uint32_t ip_list_len, uint32_t timeout_ms);
/**************************************************************************************
 * end network tcp
 **************************************************************************************/

/**************************************************************************************
 * begin tls operations
 **************************************************************************************/
/**
 * @brief Define structure for TLS connection parameters
 *
 */
typedef struct {
    const char      *ca_crt_pem;
    uint16_t        ca_crt_pem_len;
    /**
     * Device with PSK
     */
    const uint8_t   *psk_raw;           // PSK raw data
    const char      *psk_id;            // PSK ID
    uint16_t        psk_raw_length;     // PSK length
    const char      *sni;               // sni
    const char      *domain_checked;    // check the server crt domain
    uint32_t        timeout_ms;         // SSL handshake timeout in millisecond
    int32_t         (*f_vrfy)(void *,mbedtls_x509_crt *,int32_t,uint32_t *);

} SSLConnectParams;

typedef SSLConnectParams TLSConnectParams;

/**
 * @brief Tls setup and sharkhand
 *
 * @param[in] connect_params @see TLSConnectParams
 * @param[in] ip server host
 * @param[in] port server port
 * @param[out] fd tls fd
 * @return @see IotReturnCode
 */
int32_t HAL_TlsConnect(const TLSConnectParams *connect_params, const char *ip, uint16_t port, uint64_t *fd);

/**
 * @brief Disconect and free
 *
 * @param[in] fd tls handle
 */
void HAL_TlsDisconnect(uint64_t fd);

/**
 * @brief Write msg with tls
 *
 * @param[in] fd tls handle
 * @param[in] buf msg to write
 * @param[in] write_len number of bytes to write
 * @param[in] timeout_ms timeout millsecond
 * @param[out] written_len number of bytes writtern
 * @return @see IotReturnCode
 */
int32_t HAL_TlsWrite(uint64_t fd, const void *buf, uint32_t write_len, uint32_t timeout_ms, uint32_t *written_len);

/**
 * @brief Read msg with tls
 *
 * @param[in] fd tls handle
 * @param[out] buf msg buffer
 * @param[in] read_len buffer len
 * @param[in] timeout_ms timeout millsecond
 * @param[out] readed_len number of bytes read
 * @return @see IotReturnCode
 */
int32_t HAL_TlsRead(uint64_t fd, void *buf, uint32_t read_len, uint32_t timeout_ms, uint32_t *readed_len);

/**
 * @brief set server certificate verify
 *
 * @param[in] hostname server ceritificate host name
 * @param[in] crt server ceritificate
 * @param[in] depth server ceritificate level
 * @param[in] flags ceritificate verify errcode
 * @return @see IotReturnCode
 */
int32_t HAL_TlsServerCrtVerify(void *hostname, mbedtls_x509_crt *crt, int32_t depth, uint32_t *flags);
/**************************************************************************************
 * end tls operations
 **************************************************************************************/

/**************************************************************************************
 * begin timer
 **************************************************************************************/
/**
 * @brief time format string
 * @param[out] time_str store time formated str. time format "2021-05-31 15:58:46"
 * @return IotReturnCode
 */
int32_t HAL_TimeCurrentStr(char *time_str);

/**
 * @brief Get utc time ms timestamp.
 * @param[out] ms timestamp
 * @return @see IotReturnCode
 */
int32_t HAL_TimeCurrentMs(uint64_t *ms);

/**
 * @brief Set system time using ms timestamp
 *
 * @param[in] timestamp_ms
 * @return @see IotReturnCode
 */
int32_t HAL_TimeSystimeMsSet(uint64_t timestamp_ms);

/**
 * @brief Get system tick
 *
 * @param[out] system tick
 * @return @see IotReturnCode
 */
int32_t HAL_TimeSysTickMsGet(uint64_t *sys_tick_ms);
/**************************************************************************************
 * end timer
 **************************************************************************************/

/**************************************************************************************
 * begin device info
 **************************************************************************************/
/**************************************************************************************
 * begin network(4g current)
 **************************************************************************************/
/**
 * @brief network(4g current) connect
 * @return @see IotReturnCode
 */
int32_t HAL_4GConnect(void);

/**
 * @brief network(4g current) disconnect
 * @return @see IotReturnCode
 */
void HAL_4GDisconnect(void);

/**
 * @brief network(4g current) connect status
 * @return @see IotReturnCode
 */
int32_t HAL_4GConnectStatusGet(uint8_t *basestation_attached, uint8_t *pdp_acitved, uint8_t *sim_ready);
/**************************************************************************************
 * end network(4g current)
 **************************************************************************************/

/**************************************************************************************
 * begin device info
 **************************************************************************************/
/**
 * @brief MAX size of product ID.
 *
 */
#define MAX_SIZE_OF_PRODUCT_ID (10)

/**
 * @brief MAX size of device name.
 *
 */
#define MAX_SIZE_OF_DEVICE_NAME (10)

/**
 * @brief MAX size of device secret.
 *
 */
#define MAX_SIZE_OF_DEVICE_SECRET (64)

/**
 * @brief Max size of base64 encoded PSK = 64, after decode: 64/4*3 = 48.
 *
 */
#define MAX_SIZE_OF_DECODE_PSK_LENGTH 48

/**
 * @brief basestation infomation
 *
 */
typedef struct {
    uint32_t cell_id;                  // cid
    uint32_t location_area_code;       // lac
    uint32_t mobile_country_code;      // mcc
    uint32_t mobile_network_code;      // mnc
    int32_t  signal_strength;          // rssi
}BaseStationInfo;

/**
 * @brief basestation infomation
 *
 */
typedef enum {
    NETWORK_TYPE_WIFI   = 1,
    NETWORK_TYPE_2G     = 2,
    NETWORK_TYPE_3G     = 3,
    NETWORK_TYPE_4G     = 4,
    NETWORK_TYPE_5G     = 5,
}NetworkType;

#define MAX_SIZE_OF_FIRMVER     (32)
/**
 * @brief Device info needed to connect mqtt server.
 *
 */
typedef struct {
    uint8_t product_id[MAX_SIZE_OF_PRODUCT_ID + 1];
    uint8_t device_name[MAX_SIZE_OF_DEVICE_NAME + 1];
    uint8_t device_secret[MAX_SIZE_OF_DEVICE_SECRET + 1];
    uint8_t device_secret_decode[MAX_SIZE_OF_DECODE_PSK_LENGTH + 1];
    int32_t device_secret_decode_len;
    uint8_t device_app_firmver[MAX_SIZE_OF_FIRMVER + 1];
} DeviceInfo;

/**
 * @brief get remain fs space(byte)
 * @param [out] availspace fs left space
 * @return @see IotReturnCode
 */
int32_t HAL_FsAvailapaceGet(uint32_t *availspace);

/**
 * @brief get battery(0~100)
 * @param [out] battery battery level
 * @return @see IotReturnCode
 */
int32_t HAL_BatteryGet(uint32_t *battery);

/**
 * @brief get network type 1 : wifi 2 : 2G 4 : 4G 5 : 5G
 * @param [out] network_type network type
 * @return @see IotReturnCode
 */
int32_t HAL_NetworkTypeGet(NetworkType *network_type);

/**
 * @brief get signal level(csq, from 0 to 31)
 * @param [out] signal_level csq value
 * @return @see IotReturnCode
 */
int32_t HAL_SignalLevelGet(uint32_t *signal_level);

/**
 * @brief get reamin ram(byte)
 * @param [out] ram_rest ram left
 * @return @see IotReturnCode
 */
int32_t HAL_RamRestGet(uint32_t *ram_rest);

/**
 * @brief get imei
 *
 * @param[out] dst_buf buf to store imei value
 * @param[in] buf_len buf size
 * @return @see IotReturnCode
 */
int32_t HAL_ImeiGet(char *dst_buf, uint32_t buf_len);

/**
 * @brief get imsi
 *
 * @param[in] dst_buf buf to store imsi value
 * @param[in] buf_len buf size
 * @return @see IotReturnCode
 */
int32_t HAL_ImsiGet(char *dst_buf, uint32_t buf_len);

/**
 * @brief get iccid
 *
 * @param[in] dst_buf buf to store imsi value
 * @param[in] buf_len buf size
 * @return @see IotReturnCode
 */
int32_t HAL_IccidGet(char *dst_buf, uint32_t buf_len);

/**
 * @brief get location info
 *
 * @param[out] base_station_info buf to store location info
 * @return @see IotReturnCode
 */
int32_t HAL_LocationInfoGet(BaseStationInfo *base_station_info);
/**************************************************************************************
 * end device info
 **************************************************************************************/

/**************************************************************************************
 * begin system
 **************************************************************************************/
/**
 * @brief poweron type
 *
 */
typedef enum {
    POWERON_TYPE_KEYPRESS = 0,
    POWERON_TYPE_SOFTRESET
} PoweronType;

// 按键关机时会播报语音提醒语音“正在关机，感谢使用微信支付”，部分厂商需要处理一些数据的保存而延迟关机。所以在正式关机之前需要等几秒的时间，厂商可以自行适配，默认为5000ms
#define SYSTEM_POWEROFF_WAIT_TIME_MS     (5*1000)

/**
 * @brief reboot system
 *
 * @return NULL
 */
void HAL_SystemReboot(void);

/**
 * @brief power off system
 *
 * @return NULL
 */
void HAL_SystemPoweroff(void);

/**
 * @brief get system power on type
 * @param[out] poweron_type power on type
 *
 * @return @see IotReturnCode
 */
int32_t HAL_SystemPoweronTypeGet(PoweronType *poweron_type);
/**************************************************************************************
 * end system
 **************************************************************************************/

/**************************************************************************************
 * begin audio operations
 **************************************************************************************/
#define MAX_CHANNEL         (16)
#define BITS_PER_SAMPLE     (16)
#define CHANNEL             (1)
#define SAMPLE_RATE         (16000)
#define ID_RIFF             (0x46464952)
#define ID_WAVE             (0x45564157)
#define ID_FMT              (0x20746d66)
#define ID_DATA             (0x61746164)

#define FORMAT_WAVE (0)
#define FORMAT_PCM  (1)
typedef struct {
    uint32_t riff_id;
    uint32_t riff_sz;
    uint32_t riff_fmt;
    uint32_t fmt_id;
    uint32_t fmt_sz;
    uint16_t audio_format;
    uint16_t num_channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
    uint32_t data_id;
    uint32_t data_sz;
} WaveHeader;


// 音量大小必需为10档：0~9
#define WXPAY_AUDIOLEVEL_MIN        0
#define WXPAY_AUDIOLEVEL_MAX        10

// 出厂默认音量必需为第7档
#define WXPAY_AUDIOLEVEL_DEFAULT    7

// tts算法在合成语音时可以调整语音增幅进而调整输出的音量, 厂商可以根据该宏来自行调整
#define WXPAY_AUDIOLEVEL_SCALE      (1.1)

/**
 * @brief audio init
 *
 * @return @see IotReturnCode
 */
int32_t HAL_AudioInit(void);

/**
 * @brief play mp3 file
 *
 * @param[in] mp3_file mp3 file name
 * @return @see IotReturnCode
 */
int32_t HAL_AudioVoiceSet(uint32_t voice_level);

/**
 * @brief play mp3 file
 *
 * @param[in] mp3_file mp3 file name
 * @return @see IotReturnCode
 */
int32_t HAL_AudioVoiceGet(uint32_t *voice_level);
/**
 * @brief play mp3 file
 *
 * @param[in] mp3_file mp3 file name
 * @return @see IotReturnCode
 */
int32_t HAL_AudioMp3FilePlay(const char *mp3_file);

/**
 * @brief play pcm
 *
 * @param[in] pcm_data pcm name
 * @param[in] data_len pam data length
 * @return @see IotReturnCode
 */
int32_t HAL_AudioPcmPlay(void *pcm_data, uint32_t data_len);

/**
 * @brief audio play
 *
 * @param[out] play_status pcm name
 * @return @see IotReturnCode
 */
int32_t HAL_AudioPlayStatusGet(int32_t *play_status);

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
void HAL_AudioTtsFilePathGenerate(char *dst_file_path, uint32_t dst_file_path_len, const char *src_file_path, const char *play_lang, const char *tts_type);
/**************************************************************************************
 * end audio operations
 **************************************************************************************/

/**************************************************************************************
 * begin resource operations: including app firmware/sys firmware/ad file/pos file.
 **************************************************************************************/
/**
 * @brief set app firmware preload
 *
 * @param[in] file_name file name
 * @param[in] file_len file lenght
 * @return @see IotReturnCode
 */
int32_t HAL_AppFirmwarePreload(const char *file_name, uint32_t file_len);

/**
 * @brief get app firmware ver
 *
 */
int8_t *HAL_AppFirmwareVerGet(void);

/**
 * @brief set sys firmware preload
 *
 * @param[in] filename file name
 * @param[in] total_len file lenght
 * @return @see IotReturnCode
 */
int32_t HAL_SysFirmwarePreload(const char *file_name, uint32_t file_len);

/**
 * @brief get sys firmware version
 *
 */
int8_t *HAL_SysFirmwareVerGet(void);
/**************************************************************************************
 * end resource operations: including app firmware/sys firmware/ad file/pos file.
 **************************************************************************************/
/**************************************************************************************
 * battery charge operations
 **************************************************************************************/
typedef enum {
    CHARGE_STATUS_UNCHARGE          = 0,       // 未充电
    CHARGE_STATUS_CHARGING          = 1,       // 充电中
    CHARGE_STATUS_CHARGEFULL        = 2,       // 充满
}ChargeStatus;

typedef enum {
    CHARGE_TRIGGER_EVENT_UNCHARGING             = 0,      // 拔掉适配器，结束充电
    CHARGE_TRIGGER_EVENT_CHARGING               = 1,      // 插入适配器，开始充电
    CHARGE_TRIGGER_EVENT_BATTERY_LOW            = 2,      // 电量低，低于20%时每20分钟触发1次，低于10%时每10分钟触发1次
    CHARGE_TRIGGER_EVENT_BATTERY_FULL           = 3,      // 电池充满，充满触发1次
    CHARGE_TRIGGER_EVENT_RECHARGE               = 4,      // 电池充满电之后，由于一直插着适配器，触发复充。此时只调整led灯，不播报(避免打扰用户)
    CHARGE_TRIGGER_EVENT_BATTERY_LOW_POWEROFF   = 5,      // 电量过低(厂商自行确定)，上报该事件，触发机器自动关机
}ChargeTriggerEvent;
/**
 * @brief get charge status(see @ChargeStatus)
 * @param [out] charge_status charge status
 * @return @see IotReturnCode
 */
int32_t HAL_ChargeStatusGet(ChargeStatus *charge_status);
/**
 * @brief register battery charge callback
 *
 * @param[in] param callback function param(see @ChargeTriggerEvent)
 * @param[in] callback call back function
 * @return @see IotReturnCode
 */
int32_t HAL_BatteryChargeRegister(void *param, void *callback);
/**************************************************************************************
 * end battery charge operations
 **************************************************************************************/

/**************************************************************************************
 * begin led operations
 **************************************************************************************/

typedef enum {
    LED_STATUS_OFF,
    LED_STATUS_RED,
    LED_STATUS_GREEN,
    LED_STATUS_WHITE,
} LedStatus;

void HAL_LedStatusSet(LedStatus status);

/**************************************************************************************
 * end led operations
 **************************************************************************************/

/**************************************************************************************
 * begin 旧固件静默重启标记: 因为老的固件静默重启由厂商实现，当从老的固件升级到新的固件后需要处理静默重启兼容逻辑
 **************************************************************************************/
/**
 * @brief get old firmware silent reboot flag
 *
 * @return @see 1 : silent reboot. 0 : normal reboot
 */
uint8_t HAL_OldFirmwareSilentRbootFlagGet(void);

/**
 * @brief clear old firmware silent reboot flag
 *
 * @return @see 1 : silent reboot. others : normal reboot
 */
void HAL_OldFirmwareSilentRbootFlagClear(void);
/**************************************************************************************
 * end 旧固件静默重启标记: 因为老的固件静默重启由厂商实现，当从老的固件升级到新的固件后需要处理静默重启兼容逻辑
 **************************************************************************************/

/**************************************************************************************
 * begin 厂商可能需要进入特殊模式用来进行测试而不走后面的联网逻辑
 **************************************************************************************/
/**
 * @brief check factory mode
 * @param[out] factory_mode 0 : not in factory mode; 1 : in factory mode; 2 : in running age mode
 * @return @see IotReturnCode
 */
int32_t HAL_FactoryModeCheck(uint8_t *factory_mode);
/**************************************************************************************
 * end 厂商可能需要进入特殊模式用来进行测试而不走后面的联网逻辑
 **************************************************************************************/

/**************************************************************************************
 * begin 厂商可能有特殊的获取三元组的逻辑,可放在这里单独实现
 **************************************************************************************/
int32_t HAL_FactorySpecialDeviceInfoGet(DeviceInfo *device_info);
/**************************************************************************************
 * end 厂商可能有特殊的获取三元组的逻辑,可放在这里单独实现
 **************************************************************************************/

/**************************************************************************************
 * begin 厂商可能有特殊的参数文件加载逻辑,可放在这里单独实现
 **************************************************************************************/
// app参数文件
int32_t HAL_AppParamSpecialInfoGet(uint32_t *keep_alive, uint32_t *report_ivl, uint32_t *ota_type, uint32_t *monitor_switch, uint32_t *link_channel, uint32_t *main_link, uint8_t *play_lang);
int32_t HAL_AppParamSpecialInfoSet(uint32_t keep_alive, uint32_t report_ivl, uint32_t ota_type, uint32_t monitor_switch, uint32_t link_channel, uint32_t main_link, uint8_t *play_lang);
// 广告参数文件
int32_t HAL_AdvParamSpecialInfoGet(uint32_t *ad_switch, uint32_t *ad_plan, uint32_t *ad_ivl_pay, uint32_t *ad_ivl_second, uint32_t *ad_start_time, uint32_t *ad_end_time, char *ad_file);
int32_t HAL_AdvParamSpecialInfoSet(uint32_t ad_switch, uint32_t ad_plan, uint32_t ad_ivl_pay, uint32_t ad_ivl_second, uint32_t ad_start_time, uint32_t ad_end_time, char *ad_file);
// server ip 参数文件
int32_t HAL_ServerIpParamSpecialInfoGet(char *srv_ip);
int32_t HAL_ServerIpParamSpecialInfoSet(char *srv_ip);
// httpdns ip 参数文件
int32_t HAL_HttpdnsIpParamSpecialInfoGet(char *httpdns_ip);
int32_t HAL_HttpdnsIpParamSpecialInfoSet(char *httpdns_ip);
/**************************************************************************************
 * end 厂商可能有特殊的参数文件加载逻辑,可放在这里单独实现
 **************************************************************************************/
#define MAX_BUF_SIZE_CPUID        64
#define MAX_BUF_SIZE_FLASHID      64
#define MAX_BUF_SIZE_SENSORID     32
#define MAX_BUF_SIZE_BLC          32

/**
 * @brief get cpuid
 *
 * @param[out] dst_buf buf to store cpuid value
 * @param[in] buf_len buf size
 * @return @see IotReturnCode
 */
int32_t HAL_CPUIDGet(char *dst_buf, uint32_t buf_len);

/**
 * @brief get flashid
 *
 * @param[out] dst_buf buf to store flashid value
 * @param[in] buf_len buf size
 * @return @see IotReturnCode
 */
int32_t HAL_FlashIDGet(char *dst_buf, uint32_t buf_len);

/**************************************************************************************
 * begin 扫码相关实现
 **************************************************************************************/

/**
 * @brief get sensorid
 *
 * @param[out] dst_buf buf to store sensorid value
 * @param[in] buf_len buf size
 * @return @see IotReturnCode
 */
int32_t HAL_CameraSensorIDGet(char *dst_buf, uint32_t buf_len);

/**
 * @brief get blc
 *
 * @param[out] dst_buf buf to store blc value
 * @param[in] buf_len buf size
 * @return @see IotReturnCode
 */
int32_t HAL_CameraBLCGet(char *dst_buf, uint32_t buf_len);

// 打开摄像头
/**
 * @brief open camera
  * @return @see IotReturnCode, success:0, fail:-1, 重复打开也返回0
 */
int32_t HAL_CameraOpen(void);

// 关闭摄像头
/**
 * @brief close camera
  * @return @see IotReturnCode, success:0, fail:-1, 重复关闭也返回0
 */
int32_t HAL_CameraClose(void);

// 扫码成功回调函数
/**
 * @brief camera decode success callback
 * @param authcode
  * @return @see IotReturnCode, success:0, fail:-1
 */
typedef int32_t (*camera_decode_success_cb_t)(const char *authcode);

// 注册扫码成功回调函数
/**
 * @brief register camera decode success callback
 * @param callback
  * @return @see IotReturnCode, success:0, fail:-1，重复注册也返回0
 */
int32_t HAL_CameraDecodeRegister(camera_decode_success_cb_t callback);

// 发送付款码到上位机
/**
 * @brief send authcode to pos
 * @param authcode
  * @return @see IotReturnCode, success:0, fail:-1，timeout: -1500
 */
int32_t HAL_SendAuthCodeToPos(const char *authcode);

// 获取摄像头灰度图的字节数
/**
 * @brief camera capture picture
 * @return size in bytes
 */
uint32_t HAL_CameraGrayscaleImageSizeGet(void);

// 获取摄像头当前拍摄到的照片内容，为了节省内存，统一使用灰度图
/**
 * @brief camera capture picture
 * @param test_mode 0-standard picture, >0 - test mode picture
 * @param pic_buf
 * @param pic_buf_size pic_buf size
  * @return @see IotReturnCode, success:0, fail:-1, picture size in bytes:>0
 */
int32_t HAL_CameraCaptureGrayscaleImage(uint32_t test_mode, uint8_t *pic_buf, uint32_t pic_buf_size);

// 摄像头是否正在正常运行。实现说明：扫码盒子的摄像头一直处于拍照状态，通过判断摄像头缓存中是否有数据来判断摄像头是否正常运行。
/**
 * @brief camera is operating normally
 * @param[out] flag 1:normal, 0:abnormal
  * @return @see IotReturnCode, success:0, fail:-1
 */
int32_t HAL_CameraOperatingNormally(uint8_t *flag);

/**************************************************************************************
 * end 扫码相关实现
 **************************************************************************************/

// RSA2048公钥加密
// input内容不能超过256字节，也就是input_len不能超过256
// output内容长度固定是256字节
int32_t HAL_RSA2048PublicKeyEncrypt(const char *public_key_pem, uint8_t *input, uint32_t input_len, uint8_t output[256]);

// key固定32字节, iv固定12字节, tag固定16字节
int32_t HAL_AES256GCMEncrypt(uint8_t *input, uint32_t input_len, const uint8_t key[32], const uint8_t iv[12],
    const uint8_t *aad, uint32_t aad_len, uint8_t *output, uint32_t *output_len, uint8_t tag[16]);

/**************************************************************************************
 * begin 串口相关实现
 **************************************************************************************/
int32_t HAL_UartOpen(void);

int32_t HAL_UartClose(void);

int32_t HAL_UartWrite(const void *data, uint32_t data_len);
int32_t HAL_UartRead(void *buf, uint32_t buf_size);

typedef int32_t (*uart_read_cb_t)(void);
int32_t HAL_UartReadRegister(uart_read_cb_t callback);
/**************************************************************************************
 * end 串口相关实现
 **************************************************************************************/


/*
 RSA2048公钥加密
 input内容不能超过256字节，也就是input_len不能超过256
 output内容长度固定是256字节
*/
int32_t HAL_RSA2048PublicKeyEncrypt(const char *public_key_pem, uint8_t *input, uint32_t input_len, uint8_t output[256]);


/*
  key固定32字节, iv固定12字节, tag固定16字节
*/
int32_t HAL_AES256GCMEncrypt(uint8_t *input, uint32_t input_len, const uint8_t key[32], const uint8_t iv[12],const uint8_t *aad, uint32_t aad_len, uint8_t *output, uint32_t *output_len, uint8_t tag[16]);



/**************************************************************************************
 * file path config: should config by platfrom
 **************************************************************************************/
// local: log/server ip/app param/connect status/icons id
#ifdef __PLATFORM_WXPAY_CONFIG_FILE_PATH            // 厂商根据自己的平台设置各文件的目录和名字,也可以通过一个采用包含自己头文件的形式来实现
#define LOCAL_LOG_SAVE_FILE_NAME        "./tmp/upload-fail-save.log"
#define LOCAL_IP_LIST_FILE              "./tmp/server_ip_iotda.txt"
#define LOCAL_HTTPDNS_IP_LIST_FILE      "./tmp/httpdns_ip.txt"
#define LOCAL_APP_PARAM_FILE            "./tmp/app_param.json"
#define LOCAL_AD_PARAM_FILE             "./tmp/adv_param.json"
#define LOCAL_HOST_INFO_FILE            "./tmp/host_info.json"
#define LOCAL_SILENT_REBOOT_FILE        "./tmp/silent_status.txt"
#define RESOURCE_BREAK_POINT_FILE_PATH  "./tmp/break_point.dat"
#define RESOURCE_TTS_FILE_DIR           "./voices"
#define RESOURCE_AD_FILE_DIR            "./tmp"
#define RESOURCE_DOWNLOAD_FILE_DIR      "./tmp"
#define RESOURCE_SYS_FIRWARE_DIR        "./tmp"
#define RESOURCE_APP_FIRMWARE_PATH      "./tmp/app_ota_fw.bin"
#define RESOURCE_LIST_FILE_PATH         "./tmp/resource_list.json"
#define LOCAl_DEVICE_INFO_FILE          "./device_info.json"
#define LOCAL_SILENT_REBOOT_FILE        "./tmp/silent_status.txt"
#define LOG_INFO_FILE_NAME              "./tmp/log.info"
#define LOG_FILE_NAME_PREFIX            "./tmp/log_file"

#define TMP_TTS_OUTPUT_FILE             "./tmp/aud_tts_output.wav"

#define APP_CONFIG_MAIN_FILE_NAME "B:/mcfg.dat"
#define APP_CONFIG_EN_FILE_NAME "B:/ecfg.dat"
#define APP_CONFIG_DE_FILE_NAME "B:/dcfg.dat"
#define APP_CONFIG_FINISH_FILE_NAME "B:/flag.dat"
#define APP_CONFIG_CONNECT_SERVER_SUCCESS_FILE "U:/conn.dat"

#define WXPP_APP_SCAN_EVENT_META_FILE_NAME "./tmp/sem.dat"
#define APP_SCAN_FILE_0 "./tmp/s0.dat"
#define APP_SCAN_FILE_1 "./tmp/s1.dat"
#define APP_SCAN_FILE_2 "./tmp/s2.dat"
#define APP_SCAN_FILE_3 "./tmp/s3.dat"
#define APP_SCAN_FILE_4 "./tmp/s4.dat"
#define APP_SCAN_FILE_5 "./tmp/s5.dat"
#define APP_SCAN_FILE_6 "./tmp/s6.dat"
#define APP_SCAN_FILE_7 "./tmp/s7.dat"
#define APP_SCAN_FILE_8 "./tmp/s8.dat"
#define APP_SCAN_FILE_9 "./tmp/s9.dat"

#else
#include "your_file_directory_config.h"
#endif
// System firmware name saved in local
#define SYS_OTA_FILE_NAME "sys_ota_fw.bin"

// ota reamin space
#define MAX_SIZE_OF_OTA_REAMIN  (512*1024)

// resource file len
#define RESOURCE_LIST_FILE_MAX_LEN      (5*1024)

/**
 * @brief Max size of a host name.
 *
 */
#define HOST_STR_LENGTH 64

/**
 * @brief Max size of httpdns ip.
 *
 */
#define MAX_HTTPDNS_IP_LEN 16

/**
 * @brief Max size of device info file.
 *
 */
#define MAX_DEVICE_INFO_FILE_LEN 512

/**
 * @brief Max size of device config file.
 *
 */
#define MAX_DEVICE_CONFIG_FILE_LEN 512

#define IOT_FUNC_ENTRY
#define IOT_FUNC_EXIT \
    {                 \
        return;       \
    }
#define IOT_FUNC_EXIT_RC(x) \
    {                       \
        return x;           \
    }
#endif

#define UPLOAD_ERR(fmt, ...) printf("[UPLOAD_ERR][%s:%d] " fmt "\r\n", __func__, __LINE__, ##__VA_ARGS__)
#define Log_e(fmt, ...) printf("[LOG_ERR][%s:%d] " fmt "\r\n", __func__, __LINE__, ##__VA_ARGS__)
#define Log_d(fmt, ...) printf("[LOG_DUBUG][%s:%d] " fmt "\r\n", __func__, __LINE__, ##__VA_ARGS__)

#if defined(__cplusplus)
}
#endif // SPEAKER_SDK_HAL_PLATFORM_H_
