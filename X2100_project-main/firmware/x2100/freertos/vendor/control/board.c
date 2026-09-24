#include <stdio.h>
#include <common.h>
#include <driver/gpio.h>
#include <stdbool.h>
#include <os/freertos/include/FreeRTOS.h>
#include <os/freertos/include/task.h>
#include <driver/hrtimer.h>
#include <os/mutex.h>
#include <os/freertos/include/semphr.h>
#include <os.h>
#include <soc/gpio.h>

#include "board.h"

#define ABS_FLOAT(x) ((x) < 0.0f ? -(x) : (x))

static thread_ptr_t warn_thread = NULL;
static struct mutex frame_mutex;

// 硬件测试接管标志位。当置为true时，后台业务线程停止控制硬件
static volatile bool g_is_hardware_testing = false;

// 预警测试模式标志位。当置为true时，表示当前处理的是UART/CAN下发的测试指令默认参数
// Warn_frame_pkt_update 将跳过写入真实雷达数据，避免覆盖测试参数
static volatile bool g_is_warn_test_mode = false;

// 预警测试合并：记录上次收到指令的系统tick，Warn_task据此判断500ms空闲超时后自动清零
static volatile uint32_t g_warn_test_last_tick = 0;

// 不同方向/预警类型使用固定测试ID，确保合并时航迹互不冲突
#define TEST_ID_RCW       0x01
#define TEST_ID_BSD_LEFT  0x02
#define TEST_ID_AOA_LEFT  0x03
#define TEST_ID_LCA_LEFT  0x04
#define TEST_ID_BSD_RIGHT 0x05
#define TEST_ID_AOA_RIGHT 0x06
#define TEST_ID_LCA_RIGHT 0x07

static int gFrameID = 0;
static Mmw_output_message_DetInfo gDetInfo = {0};
static Mmw_output_message_TrkInfo gTrkInfo = {0};
#if EGO_VLC_ENABLE
static Mmw_output_message_EgoVlcInfo gEgoVlcInfo = {0};
#endif
#if WARNING_ENABLE
static Mmw_output_message_WarnInfo gWarnInfo = {0};
#endif

typedef enum {
    LED_MODE_OFF = 0,
    LED_MODE_ON,
    LED_MODE_FLASH
} LedMode;

static LedMode gLeftLedMode = LED_MODE_OFF;
static LedMode gRightLedMode = LED_MODE_OFF; 
static bool gLeftLedState = false;
static bool gRightLedState = false;

static struct hrtimer gLeftLedTimer;
static struct hrtimer gRightLedTimer;
static const uint64_t FLASH_INTERVAL_US = 166666;

static void left_led_timer_callback(struct hrtimer *timer)
{
    gLeftLedState = !gLeftLedState;
    gpio_set_value(LEFT_LED, gLeftLedState ? LED_ON : LED_OFF);
    hrtimer_restart(timer, FLASH_INTERVAL_US);
}

static void right_led_timer_callback(struct hrtimer *timer)
{
    gRightLedState = !gRightLedState;
    gpio_set_value(RIGHT_LED, gRightLedState ? LED_ON : LED_OFF);
    hrtimer_restart(timer, FLASH_INTERVAL_US);
}

void led_set_mode(bool is_left, LedMode mode)
{
    if (is_left) {
        if (gLeftLedMode == mode) {
            return;
        }

        if (gLeftLedMode == LED_MODE_FLASH) {
            hrtimer_cancel(&gLeftLedTimer);
        }

        gLeftLedMode = mode;

        switch (mode) {
            case LED_MODE_ON:
                gpio_set_value(LEFT_LED, LED_ON);
                break;
            case LED_MODE_FLASH:
                gLeftLedState = true;
                gpio_set_value(LEFT_LED, LED_ON);
                hrtimer_start(&gLeftLedTimer, FLASH_INTERVAL_US);
                break;
            case LED_MODE_OFF:
            default:
                gpio_set_value(LEFT_LED, LED_OFF);
                break;
        }
    } else {
        if (gRightLedMode == mode) {
            return;
        }

        if (gRightLedMode == LED_MODE_FLASH) {
            hrtimer_cancel(&gRightLedTimer);
        }

        gRightLedMode = mode;

        switch (mode) {
            case LED_MODE_ON:
                gpio_set_value(RIGHT_LED, LED_ON);
                break;
            case LED_MODE_FLASH:
                gRightLedState = true;
                gpio_set_value(RIGHT_LED, LED_ON);
                hrtimer_start(&gRightLedTimer, FLASH_INTERVAL_US);
                break;
            case LED_MODE_OFF:
            default:
                gpio_set_value(RIGHT_LED, LED_OFF);
                break;
        }
    }
}

// 可配置参数：BSD/AOA启动车速阈值(单位：km/h)
static float gEgoSpeedThresholdKph = 10.0f;

// 报警优先级枚举
typedef enum {
    WARN_PRIORITY_NONE = 0,
    WARN_PRIORITY_LCA,
    WARN_PRIORITY_AOA,
    WARN_PRIORITY_BSD,
    WARN_PRIORITY_RCW
} WarnPriority;

// 获取航迹的距离信息（使用纵向距离x_output，通过trkID查找）
static float get_track_distance(uint8_t trkID)
{
    for (int i = 0; i < gTrkInfo.header.numTrks; i++) {
        if (gTrkInfo.trkObj[i].isvalid && gTrkInfo.trkObj[i].trkID == trkID) {
            return (gTrkInfo.trkObj[i].x_output / 8.0f - 255.0f);
        }
    }
    return -1.0f;
}

// 获取航迹的径向速度信息（使用纵向速度vx_output，通过trkID查找）
static float get_track_radial_velocity(uint8_t trkID)
{
    for (int i = 0; i < gTrkInfo.header.numTrks; i++) {
        if (gTrkInfo.trkObj[i].isvalid && gTrkInfo.trkObj[i].trkID == trkID) {
            return (gTrkInfo.trkObj[i].vx_output / 20.0f - 102.0f);
        }
    }
    return 0.0f;
}

// 计算TTC (Time To Collision)
// 支持负坐标：distance与radial_velocity符号相反时表示靠近
static float calculate_ttc(float distance, float radial_velocity)
{
    // 同号表示远离或静止，异号表示靠近
    if (distance * radial_velocity >= 0) {
        return 999.0f;
    }
    float ttc = -distance / radial_velocity;
    if (ttc < 0) ttc = 999.0f;
    return ttc;
}

// 获取指定报警类型的最小TTC
static float get_min_ttc_for_warn_type(bool is_left, uint8_t* trkIDs, uint8_t trkCnt)
{
    float min_ttc = 999.0f;
    for (int i = 0; i < trkCnt; i++) {
        float distance = get_track_distance(trkIDs[i]);
        if (distance != 0.0f) {
            float radial_v = get_track_radial_velocity(trkIDs[i]);
            float ttc = calculate_ttc(distance, radial_v);
            if (ttc < min_ttc) {
                min_ttc = ttc;
            }
        }
    }
    return min_ttc;
}

// 根据距离确定LED显示模式（支持正负坐标，取绝对值判断）
// 注意：distance == -1 表示无效值（无目标），不应触发LED
static LedMode get_led_mode_by_distance(float distance)
{
    // -1 表示无效值，直接返回 OFF
    if (distance == -1.0f) {
        return LED_MODE_OFF;
    }
    float abs_dist = ABS_FLOAT(distance);
    if (abs_dist > 10.0f && abs_dist <= 70.0f) {
        return LED_MODE_ON;
    } else if (abs_dist > 0.0f && abs_dist <= 10.0f) {
        return LED_MODE_FLASH;
    }
    return LED_MODE_OFF;
}

// 获取当前报警信息并确定最高优先级
static void process_warning(WarnResult* warnResult, 
                            WarnPriority* priority, 
                            uint8_t* left_trkID, uint8_t* right_trkID,
                            float* left_distance, float* right_distance,
                            bool* is_rcw)
{
    *priority = WARN_PRIORITY_NONE;
    *left_trkID = 0;
    *right_trkID = 0;
    *left_distance = -1.0f;
    *right_distance = -1.0f;
    *is_rcw = false;

    float ego_speed_kph = (gEgoVlcInfo.egoVelocity_mps / 100.0f) * 3.6f;

    float min_ttc_rcw = 999.0f;
    float min_ttc_bsd = 999.0f;
    float min_ttc_aoa = 999.0f;
    float min_ttc_lca = 999.0f;

    // 初始化左侧和右侧各自的优先级和距离
    WarnPriority left_priority = WARN_PRIORITY_NONE;
    WarnPriority right_priority = WARN_PRIORITY_NONE;
    float left_ttc = 999.0f;
    float right_ttc = 999.0f;

    if (warnResult->RCW && warnResult->RCW_cnt > 0) {
        min_ttc_rcw = get_min_ttc_for_warn_type(true, warnResult->RCW_IDs, warnResult->RCW_cnt);
        *is_rcw = true;
        *priority = WARN_PRIORITY_RCW;
        *left_distance = get_track_distance(warnResult->RCW_IDs[0]);
        *right_distance = *left_distance;
        printf("[process_warning] RCW: set both distance=%.2f\n", *left_distance);
        // RCW优先级最高，直接返回
        return;
    }

    if (ego_speed_kph >= gEgoSpeedThresholdKph) {
        // 处理左侧BSD
        if (warnResult->BSD_left && warnResult->BSD_left_cnt > 0) {
            float ttc = get_min_ttc_for_warn_type(true, warnResult->BSD_left_IDs, warnResult->BSD_left_cnt);
            if (ttc < min_ttc_bsd) min_ttc_bsd = ttc;
            if (ttc < left_ttc) {
                left_ttc = ttc;
                left_priority = WARN_PRIORITY_BSD;
                *left_trkID = warnResult->BSD_left_IDs[0];
                *left_distance = get_track_distance(*left_trkID);
                printf("[process_warning] BSD_left: set left_distance=%.2f\n", *left_distance);
            }
        }

        // 处理右侧BSD
        if (warnResult->BSD_right && warnResult->BSD_right_cnt > 0) {
            float ttc = get_min_ttc_for_warn_type(false, warnResult->BSD_right_IDs, warnResult->BSD_right_cnt);
            if (ttc < min_ttc_bsd) min_ttc_bsd = ttc;
            if (ttc < right_ttc) {
                right_ttc = ttc;
                right_priority = WARN_PRIORITY_BSD;
                *right_trkID = warnResult->BSD_right_IDs[0];
                *right_distance = get_track_distance(*right_trkID);
                printf("[process_warning] BSD_right: set right_distance=%.2f\n", *right_distance);
            }
        }

        // 处理左侧AOA
        if (warnResult->AOA_left && warnResult->AOA_left_cnt > 0) {
            float ttc = get_min_ttc_for_warn_type(true, warnResult->AOA_left_IDs, warnResult->AOA_left_cnt);
            if (ttc < min_ttc_aoa) min_ttc_aoa = ttc;
            // AOA优先级高于BSD
            if (ttc < left_ttc || left_priority < WARN_PRIORITY_AOA) {
                left_ttc = ttc;
                left_priority = WARN_PRIORITY_AOA;
                *left_trkID = warnResult->AOA_left_IDs[0];
                *left_distance = get_track_distance(*left_trkID);
                printf("[process_warning] AOA_left: set left_distance=%.2f\n", *left_distance);
            }
        }

        // 处理右侧AOA
        if (warnResult->AOA_right && warnResult->AOA_right_cnt > 0) {
            float ttc = get_min_ttc_for_warn_type(false, warnResult->AOA_right_IDs, warnResult->AOA_right_cnt);
            if (ttc < min_ttc_aoa) min_ttc_aoa = ttc;
            // AOA优先级高于BSD
            if (ttc < right_ttc || right_priority < WARN_PRIORITY_AOA) {
                right_ttc = ttc;
                right_priority = WARN_PRIORITY_AOA;
                *right_trkID = warnResult->AOA_right_IDs[0];
                *right_distance = get_track_distance(*right_trkID);
                printf("[process_warning] AOA_right: set right_distance=%.2f\n", *right_distance);
            }
        }
    }

    // 处理左侧LCA（不受车速限制）
    if (warnResult->LCA_left && warnResult->LCA_left_cnt > 0) {
        float ttc = get_min_ttc_for_warn_type(true, warnResult->LCA_left_IDs, warnResult->LCA_left_cnt);
        if (ttc < min_ttc_lca) min_ttc_lca = ttc;
        // LCA优先级最高（除RCW外）
        if (ttc < left_ttc || left_priority < WARN_PRIORITY_LCA) {
            left_ttc = ttc;
            left_priority = WARN_PRIORITY_LCA;
            *left_trkID = warnResult->LCA_left_IDs[0];
            *left_distance = get_track_distance(*left_trkID);
            printf("[process_warning] LCA_left: set left_distance=%.2f\n", *left_distance);
        }
    }

    // 处理右侧LCA（不受车速限制）
    if (warnResult->LCA_right && warnResult->LCA_right_cnt > 0) {
        float ttc = get_min_ttc_for_warn_type(false, warnResult->LCA_right_IDs, warnResult->LCA_right_cnt);
        if (ttc < min_ttc_lca) min_ttc_lca = ttc;
        // LCA优先级最高（除RCW外）
        if (ttc < right_ttc || right_priority < WARN_PRIORITY_LCA) {
            right_ttc = ttc;
            right_priority = WARN_PRIORITY_LCA;
            *right_trkID = warnResult->LCA_right_IDs[0];
            *right_distance = get_track_distance(*right_trkID);
            printf("[process_warning] LCA_right: set right_distance=%.2f\n", *right_distance);
        }
    }

    // 确定整体优先级（取两侧最高优先级）
    *priority = (left_priority > right_priority) ? left_priority : right_priority;

    // 最后检查RCW是否优先级最高
    if (warnResult->RCW && warnResult->RCW_cnt > 0) {
        if (min_ttc_rcw <= min_ttc_bsd && min_ttc_rcw <= min_ttc_aoa && min_ttc_rcw <= min_ttc_lca) {
            *priority = WARN_PRIORITY_RCW;
            *is_rcw = true;
            *left_distance = get_track_distance(warnResult->RCW_IDs[0]);
            *right_distance = *left_distance;
            printf("[process_warning] RCW final: set both distance=%.2f\n", *left_distance);
        }
    }
}

// LED延时熄灭时间（单位：毫秒）
#define LED_DELAY_OFF_MS 500

void Warn_task(void *pvParameters)
{
    WarnResult localWarnResult = {0};
    WarnPriority priority = WARN_PRIORITY_NONE;
    uint8_t left_trkID = 0, right_trkID = 0;
    float left_distance = -1.0f, right_distance = -1.0f;
    bool is_rcw = false;
    bool buzzer_state = false;
    bool last_buzzer_state = false;

    // LED延时熄灭相关变量
    LedMode left_led_mode_stored = LED_MODE_OFF;
    LedMode right_led_mode_stored = LED_MODE_OFF;
    uint32_t left_led_off_time = 0;
    uint32_t right_led_off_time = 0;

    while (1) {
        // ===== 预警测试模式：100ms 无新指令则自动清零 =====
        if (g_is_warn_test_mode) {
            uint32_t now = xTaskGetTickCount();
            if ((now - g_warn_test_last_tick) > pdMS_TO_TICKS(800)) {
                mutex_lock(&frame_mutex);
                memset(&gWarnInfo.warnResult, 0, sizeof(WarnResult));
                memset(&gTrkInfo, 0, sizeof(Mmw_output_message_TrkInfo));
                gWarnInfo.warnResult.TTC_min = 999.0f;
                mutex_unlock(&frame_mutex);
                g_is_warn_test_mode = false;
                g_warn_test_last_tick = 0;
                led_set_mode(true, LED_MODE_OFF);
                led_set_mode(false, LED_MODE_OFF);
                gpio_set_value(BUZZER, BUZZER_OFF);
                printf("[WARN_TEST] idle > 800ms -> auto-clear, all OFF.\n");
            }
        }

        mutex_lock(&frame_mutex);
        // memcpy(&localWarnResult, &gWarnInfo.warnResult, sizeof(WarnResult));
        localWarnResult = gWarnInfo.warnResult;
        mutex_unlock(&frame_mutex);

        process_warning(&localWarnResult, &priority, 
                      &left_trkID, &right_trkID,
                      &left_distance, &right_distance, &is_rcw);

        uint32_t now = xTaskGetTickCount();
        LedMode left_mode = LED_MODE_OFF;
        LedMode right_mode = LED_MODE_OFF;

        if (is_rcw) {
            printf("[is_rcw] ！！！！！！\n");
            // RCW规则：0~70米闪烁，10米以内蜂鸣器响
            float abs_left = ABS_FLOAT(left_distance);
            float abs_right = ABS_FLOAT(right_distance);
            left_mode = (abs_left > 0.0f && abs_left <= 70.0f) ? LED_MODE_FLASH : LED_MODE_OFF;
            right_mode = (abs_right > 0.0f && abs_right <= 70.0f) ? LED_MODE_FLASH : LED_MODE_OFF;

            // 更新存储的模式和时间戳
            left_led_mode_stored = left_mode;
            right_led_mode_stored = right_mode;
            if (left_mode != LED_MODE_OFF) left_led_off_time = now;
            if (right_mode != LED_MODE_OFF) right_led_off_time = now;

            buzzer_state = (abs_left > 0.0f && abs_left <= 10.0f);
        } else {
            left_mode = get_led_mode_by_distance(left_distance);
            right_mode = get_led_mode_by_distance(right_distance);

            // 更新存储的模式和时间戳
            if (left_mode != LED_MODE_OFF) {
                left_led_mode_stored = left_mode;
                left_led_off_time = now;
            }
            if (right_mode != LED_MODE_OFF) {
                right_led_mode_stored = right_mode;
                right_led_off_time = now;
            }

            // 延时熄灭逻辑：当当前帧无警告时，检查是否已超过延时时间
            if (left_mode == LED_MODE_OFF && left_led_mode_stored != LED_MODE_OFF) {
                if ((now - left_led_off_time) < pdMS_TO_TICKS(LED_DELAY_OFF_MS)) {
                    left_mode = left_led_mode_stored;
                } else {
                    left_led_mode_stored = LED_MODE_OFF;
                }
            }
            if (right_mode == LED_MODE_OFF && right_led_mode_stored != LED_MODE_OFF) {
                if ((now - right_led_off_time) < pdMS_TO_TICKS(LED_DELAY_OFF_MS)) {
                    right_mode = right_led_mode_stored;
                } else {
                    right_led_mode_stored = LED_MODE_OFF;
                }
            }

            buzzer_state = false;
        }

        led_set_mode(true, left_mode);
        led_set_mode(false, right_mode);
        gpio_set_value(BUZZER, buzzer_state ? BUZZER_ON : BUZZER_OFF);

        vTaskDelay(50 / portTICK_PERIOD_MS);
    }
}

// 灯控测试函数 - 测试LED闪烁功能
void led_test_function(LedMode left_mode, LedMode right_mode, uint32_t duration_ms)
{
    led_set_mode(true, left_mode);
    led_set_mode(false, right_mode);
    vTaskDelay(duration_ms / portTICK_PERIOD_MS);
    led_set_mode(true, LED_MODE_OFF);
    led_set_mode(false, LED_MODE_OFF);
}

void led_test(void)
{
    g_is_hardware_testing = true; // 强制接管硬件控制权

        // LED功能测试
    vTaskDelay(500 / portTICK_PERIOD_MS);  // 间隔500ms

    // 测试1: 左LED常亮1秒
    printf("LED Test 1: Left LED ON, Right LED OFF\n");
    led_test_function(LED_MODE_ON, LED_MODE_OFF, 1000);
    printf("LED Test 1: Complete\n");
    vTaskDelay(200 / portTICK_PERIOD_MS);

    // 测试2: 右LED常亮1秒
    printf("LED Test 2: Left LED OFF, Right LED ON\n");
    led_test_function(LED_MODE_OFF, LED_MODE_ON, 1000);
    printf("LED Test 2: Complete\n");
    vTaskDelay(200 / portTICK_PERIOD_MS);

    // 测试3: 左LED3Hz闪烁2秒
    printf("LED Test 3: Left LED 3Hz Flash, Right LED OFF\n");
    led_test_function(LED_MODE_FLASH, LED_MODE_OFF, 2000);
    printf("LED Test 3: Complete\n");
    vTaskDelay(200 / portTICK_PERIOD_MS);

    // 测试4: 右LED3Hz闪烁2秒
    printf("LED Test 4: Left LED OFF, Right LED 3Hz Flash\n");
    led_test_function(LED_MODE_OFF, LED_MODE_FLASH, 2000);
    printf("LED Test 4: Complete\n");
    vTaskDelay(200 / portTICK_PERIOD_MS);

    // 测试5: 双LED同时3Hz闪烁2秒
    printf("LED Test 5: Both LEDs 3Hz Flash\n");
    led_test_function(LED_MODE_FLASH, LED_MODE_FLASH, 2000);
    printf("LED Test 5: Complete\n");
    vTaskDelay(200 / portTICK_PERIOD_MS);

    // 测试6: 双LED常亮1秒（最终确认）
    printf("LED Test 6: Both LEDs ON\n");
    led_test_function(LED_MODE_ON, LED_MODE_ON, 1000);
    printf("LED Test 6: Complete\n");

    // 确保测试完成后LED关闭
    printf("LED Tests Completed. Turning off all LEDs.\n");
    led_set_mode(true, LED_MODE_OFF);
    led_set_mode(false, LED_MODE_OFF);
    
    g_is_hardware_testing = false; // 归还硬件控制权
}

// 蜂鸣器受控鸣叫基础函数
void buzzer_test_function(bool is_on, uint32_t duration_ms)
{
    gpio_set_value(BUZZER, is_on ? BUZZER_ON : BUZZER_OFF);
    vTaskDelay(duration_ms / portTICK_PERIOD_MS);
}

void buzzer_test(void)
{
    g_is_hardware_testing = true; // 强制接管硬件控制权

    // 初始等待 500ms
    vTaskDelay(500 / portTICK_PERIOD_MS);

    // 测试1: 蜂鸣器长鸣 1 秒
    printf("Buzzer Test 1: Solid Beep for 1000ms\n");
    buzzer_test_function(true, 1000);
    buzzer_test_function(false, 500); // 间隔 500ms
    printf("Buzzer Test 1: Complete\n");

    // 测试2: 蜂鸣器连续短促鸣叫 3 次（急促报警模拟）
    printf("Buzzer Test 2: 3 Short Beeps (200ms ON, 200ms OFF)\n");
    for (int i = 0; i < 3; i++) {
        buzzer_test_function(true, 200);
        buzzer_test_function(false, 200);
    }
    printf("Buzzer Test 2: Complete\n");

    // 确保测试完毕后蜂鸣器处于强制关闭状态
    printf("Buzzer Tests Completed. Turning off Buzzer.\n");
    gpio_set_value(BUZZER, BUZZER_OFF);

    g_is_hardware_testing = false; // 归还硬件控制权
}

void board_init(void)
{
    gpio_request(LEFT_LED,"left_led");
    gpio_direction_output(LEFT_LED,0);
    gpio_set_value(LEFT_LED,LED_OFF);
    soc_gpio_set_driver_strength(LEFT_LED, 3); // 设置最大驱动能力

    gpio_request(RIGHT_LED,"right_led");
    gpio_direction_output(RIGHT_LED,0);
    gpio_set_value(RIGHT_LED,LED_OFF);
    soc_gpio_set_driver_strength(RIGHT_LED, 3); // 设置最大驱动能力

    gpio_request(BUZZER,"buzzer");
    gpio_direction_output(BUZZER,0);
    gpio_set_value(BUZZER,BUZZER_OFF);
    soc_gpio_set_driver_strength(BUZZER, 3); // 设置最大驱动能力

    mutex_init(&frame_mutex);

    hrtimer_init(&gLeftLedTimer, left_led_timer_callback);
    hrtimer_init(&gRightLedTimer, right_led_timer_callback);

    // 初始化时LED以2Hz闪烁2次（2Hz = 500ms周期）
    for (int i = 0; i < 2; i++) {
        gpio_set_value(LEFT_LED, LED_ON);
        gpio_set_value(RIGHT_LED, LED_ON);
        vTaskDelay(250 / portTICK_PERIOD_MS);  // 亮250ms
        gpio_set_value(LEFT_LED, LED_OFF);
        gpio_set_value(RIGHT_LED, LED_OFF);
        vTaskDelay(250 / portTICK_PERIOD_MS);  // 灭250ms
    }
    // LED功能测试
    // led_test();

    warn_thread = thread_create("Warn_task", 32768, Warn_task, NULL);
    if (warn_thread == NULL)
    {
        printf("Failed to create Warn_task\n");
        return;
    }

    thread_set_priority(warn_thread, OS_priority_high);
}


int Warn_frame_pkt_update(Mmw_pkt_info* pktInfo, uint32_t frameID)
{
    if (!pktInfo)
    {
        return -1;
    }

    mutex_lock(&frame_mutex);

    // 预警测试模式下，跳过真实雷达数据的写入，避免覆盖测试默认参数
    if (g_is_warn_test_mode) {
        mutex_unlock(&frame_mutex);
        return 0;
    }

    gFrameID = frameID;

    memset(&gDetInfo, 0, sizeof(Mmw_output_message_DetInfo));
    memset(&gTrkInfo, 0, sizeof(Mmw_output_message_TrkInfo));
#if EGO_VLC_ENABLE
    memset(&gEgoVlcInfo, 0, sizeof(Mmw_output_message_EgoVlcInfo));
#endif
#if WARNING_ENABLE
    memset(&gWarnInfo, 0, sizeof(Mmw_output_message_WarnInfo));
#endif

    memcpy(&gDetInfo, &pktInfo->detInfo, sizeof(Mmw_output_message_DetInfo));
    memcpy(&gTrkInfo, &pktInfo->trkInfo, sizeof(Mmw_output_message_TrkInfo));
#if EGO_VLC_ENABLE
    memcpy(&gEgoVlcInfo, &pktInfo->egoVlcInfo, sizeof(Mmw_output_message_EgoVlcInfo));
#endif
#if WARNING_ENABLE
    memcpy(&gWarnInfo, &pktInfo->warnInfo, sizeof(Mmw_output_message_WarnInfo));
#endif

    mutex_unlock(&frame_mutex);

    return 0;
}

/* ================================================================
 * 预警测试协议处理
 * 上位机通过UART或CAN发送预警指令，解析后驱动LED和蜂鸣器
 * ================================================================ */
// ========== 合并辅助函数（多条指令合并时使用） =========
// 向预警ID数组中插入一个新ID（已存在则不重复添加）
static void merge_warn_id(uint8_t *ids, uint8_t *cnt, uint8_t new_id)
{
    for (int i = 0; i < *cnt; i++) {
        if (ids[i] == new_id) return;
    }
    if (*cnt < MAX_WARN_IDS) {
        ids[*cnt] = new_id;
        (*cnt)++;
    }
}

// 向航迹列表中添加/更新一条测试航迹（根据trkID去重）
static void merge_test_track(uint8_t trkID, float x_dist, float vx_vel)
{
    // 检查该ID是否已存在 → 更新距离/速度
    for (int i = 0; i < gTrkInfo.header.numTrks; i++) {
        if (gTrkInfo.trkObj[i].trkID == trkID) {
            gTrkInfo.trkObj[i].x_output  = (uint16_t)((x_dist + 255.0f) * 8.0f);
            gTrkInfo.trkObj[i].vx_output = (uint16_t)((vx_vel + 102.0f) * 20.0f);
            return;
        }
    }
    // 不存在 → 新增一条
    int idx = gTrkInfo.header.numTrks;
    if (idx >= MAX_TRACKS) return;
    gTrkInfo.trkObj[idx].isvalid      = true;
    gTrkInfo.trkObj[idx].trkID        = trkID;
    gTrkInfo.trkObj[idx].x_output     = (uint16_t)((x_dist + 255.0f) * 8.0f);
    gTrkInfo.trkObj[idx].vx_output    = (uint16_t)((vx_vel + 102.0f) * 20.0f);
    gTrkInfo.trkObj[idx].y_output     = (uint16_t)(0.0f);
    gTrkInfo.trkObj[idx].vy_output    = (uint16_t)(0.0f);
    gTrkInfo.trkObj[idx].motion_state = 1;
    gTrkInfo.header.numTrks++;
}

void warn_test_handler_ext(WarnTestResult *result)
{
    if (!result) {
        return;
    }
 
    // 收到"无预警"指令 → 立即清除所有测试数据并退出
    if (!result->warn_active) {
        mutex_lock(&frame_mutex);
        memset(&gWarnInfo.warnResult, 0, sizeof(WarnResult));
        memset(&gTrkInfo, 0, sizeof(Mmw_output_message_TrkInfo));
        gWarnInfo.warnResult.TTC_min = 999.0f;
        mutex_unlock(&frame_mutex);
        g_is_warn_test_mode = false;
        g_warn_test_last_tick = 0;
        printf("[WARN_TEST] No warning -> cleared all test data immediately.\n");
        return;
    }
 
    // 置位测试模式，记录 tick（Warn_task 据此判断 100ms 空闲超时）
    g_is_warn_test_mode = true;
    g_warn_test_last_tick = xTaskGetTickCount();
 
    // 自车速度 30 km/h（确保 BSD/AOA 通过车速阈值）
    gEgoVlcInfo.egoVelocity_mps = (int16_t)(8.33f * 100.0f);
 
    float default_dist = result->distance_near ? 5.0f : 30.0f;
 
    mutex_lock(&frame_mutex);
 
    // ---- 合并 WarnResult + 航迹（不覆盖已有数据） ----
    switch (result->direction) {
        case WARN_DIR_BACK:  // 后方 RCW
            if (result->rcw) {
                gWarnInfo.warnResult.RCW = true;
                merge_warn_id(gWarnInfo.warnResult.RCW_IDs,
                              &gWarnInfo.warnResult.RCW_cnt,
                              TEST_ID_RCW);
                merge_test_track(TEST_ID_RCW, default_dist, -5.0f);
                // printf("[WARN_TEST] MERGE RCW: ID=0x%02X, dist=%.1f\n", TEST_ID_RCW, default_dist);
            }
            break;
 
        case WARN_DIR_LEFT:
            if (result->bsd) {
                gWarnInfo.warnResult.BSD_left = true;
                merge_warn_id(gWarnInfo.warnResult.BSD_left_IDs,
                              &gWarnInfo.warnResult.BSD_left_cnt,
                              TEST_ID_BSD_LEFT);
                merge_test_track(TEST_ID_BSD_LEFT, default_dist, -5.0f);
                // printf("[WARN_TEST] MERGE BSD_left: ID=0x%02X, dist=%.1f\n", TEST_ID_BSD_LEFT, default_dist);
            }
            if (result->aoa) {
                gWarnInfo.warnResult.AOA_left = true;
                merge_warn_id(gWarnInfo.warnResult.AOA_left_IDs,
                              &gWarnInfo.warnResult.AOA_left_cnt,
                              TEST_ID_AOA_LEFT);
                merge_test_track(TEST_ID_AOA_LEFT, default_dist, -5.0f);
                // printf("[WARN_TEST] MERGE AOA_left: ID=0x%02X, dist=%.1f\n", TEST_ID_AOA_LEFT, default_dist);
            }
            if (result->lca) {
                gWarnInfo.warnResult.LCA_left = true;
                merge_warn_id(gWarnInfo.warnResult.LCA_left_IDs,
                              &gWarnInfo.warnResult.LCA_left_cnt,
                              TEST_ID_LCA_LEFT);
                merge_test_track(TEST_ID_LCA_LEFT, default_dist, -5.0f);
                // printf("[WARN_TEST] MERGE LCA_left: ID=0x%02X, dist=%.1f\n", TEST_ID_LCA_LEFT, default_dist);
            }
            break;
 
        case WARN_DIR_RIGHT:
            if (result->bsd) {
                gWarnInfo.warnResult.BSD_right = true;
                merge_warn_id(gWarnInfo.warnResult.BSD_right_IDs,
                              &gWarnInfo.warnResult.BSD_right_cnt,
                              TEST_ID_BSD_RIGHT);
                merge_test_track(TEST_ID_BSD_RIGHT, default_dist, -5.0f);
                // printf("[WARN_TEST] MERGE BSD_right: ID=0x%02X, dist=%.1f\n", TEST_ID_BSD_RIGHT, default_dist);
            }
            if (result->aoa) {
                gWarnInfo.warnResult.AOA_right = true;
                merge_warn_id(gWarnInfo.warnResult.AOA_right_IDs,
                              &gWarnInfo.warnResult.AOA_right_cnt,
                              TEST_ID_AOA_RIGHT);
                merge_test_track(TEST_ID_AOA_RIGHT, default_dist, -5.0f);
                // printf("[WARN_TEST] MERGE AOA_right: ID=0x%02X, dist=%.1f\n", TEST_ID_AOA_RIGHT, default_dist);
            }
            if (result->lca) {
                gWarnInfo.warnResult.LCA_right = true;
                merge_warn_id(gWarnInfo.warnResult.LCA_right_IDs,
                              &gWarnInfo.warnResult.LCA_right_cnt,
                              TEST_ID_LCA_RIGHT);
                merge_test_track(TEST_ID_LCA_RIGHT, default_dist, -5.0f);
                // printf("[WARN_TEST] MERGE LCA_right: ID=0x%02X, dist=%.1f\n", TEST_ID_LCA_RIGHT, default_dist);
            }
            break;
 
        default:
            printf("[WARN_TEST] Invalid direction 0x%02X -> ignored\n", result->direction);
            break;
    }
 
    gWarnInfo.warnResult.TTC_min = default_dist / 5.0f;
 
    mutex_unlock(&frame_mutex);
    printf("[WARN_TEST] Merged. Send more commands within 100ms to combine.\n");
}

void warn_test_handler(uint8_t warn_byte)
{
    WarnTestResult result;
    memset(&result, 0, sizeof(result));

    // 解析预警方向 (bits 0-1)
    result.direction = warn_byte & 0x03;

    switch (result.direction) {
        case WARN_DIR_BACK:   // 01 = 后方
            result.rcw    = (warn_byte >> 2) & 0x01;
            result.distance_near = (warn_byte >> 3) & 0x01;
            result.warn_active = result.rcw;
            // printf("[WARN_TEST] Parsed BACK: warn_byte=0x%02X, RCW=%d, near=%d\n",
            //        warn_byte, result.rcw, result.distance_near);
            break;

        case WARN_DIR_LEFT:   // 10 = 左方
        case WARN_DIR_RIGHT:  // 11 = 右方
            result.bsd    = (warn_byte >> 2) & 0x01;
            bool bsd_near = (warn_byte >> 3) & 0x01;
            result.aoa    = (warn_byte >> 4) & 0x01;
            bool aoa_near = (warn_byte >> 5) & 0x01;
            result.lca    = (warn_byte >> 6) & 0x01;
            bool lca_near = (warn_byte >> 7) & 0x01;

            result.warn_active = result.bsd || result.aoa || result.lca;
            result.distance_near = bsd_near || aoa_near || lca_near;

            // printf("[WARN_TEST] Parsed %s: warn_byte=0x%02X, BSD=%d(near=%d), AOA=%d(near=%d), LCA=%d(near=%d)\n",
            //        (result.direction == WARN_DIR_LEFT) ? "LEFT" : "RIGHT",
            //        warn_byte,
            //        result.bsd, bsd_near,
            //        result.aoa, aoa_near,
            //        result.lca, lca_near);
            break;

        default:  // 00 = 无效
            result.warn_active = false;
            printf("[WARN_TEST] Parsed invalid direction: warn_byte=0x%02X\n", warn_byte);
            break;
    }

    warn_test_handler_ext(&result);
}

void warn_test_from_binary_string(const char *binary_str)
{
    if (!binary_str || strlen(binary_str) < 8) {
        printf("[WARN_TEST] Invalid binary string: %s\n", binary_str ? binary_str : "NULL");
        return;
    }

    uint8_t warn_byte = 0;
    for (int i = 0; i < 8; i++) {
        if (binary_str[i] == '1') {
            warn_byte |= (1 << (7 - i));
        } else if (binary_str[i] != '0') {
            printf("[WARN_TEST] Invalid char in binary string: %c\n", binary_str[i]);
            return;
        }
    }

    // printf("[WARN_TEST] Binary string \"%s\" -> 0x%02X\n", binary_str, warn_byte);
    warn_test_handler(warn_byte);
}
