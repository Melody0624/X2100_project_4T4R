#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <common.h>
#include <os.h>
#include <sys/errno.h>
#include <os/freertos/include/FreeRTOS.h>
#include <os/freertos/include/task.h>
#include <os/freertos/include/semphr.h>
#include <usb/gadget_serial.h>

#include "motorcycle_output.h"
#include "adc_capture_packet.h"
#include "radar_config.h"
#include "radar_tracking.h"
#include "radar_warning.h"

#define MOTORCYCLE_PLATFORM                 0x24u
#define MOTORCYCLE_TLV_DETECTION_INFO       21u
#define MOTORCYCLE_TLV_TRACK_INFO           22u
#define MOTORCYCLE_TLV_WARNING_INFO         24u
#define MOTORCYCLE_TX_STACK                 4096u
#define MOTORCYCLE_RX_STACK                 2048u
#define MOTORCYCLE_COMMAND_BYTES           4096u
#define MOTORCYCLE_ADC_USB_CHUNK_BYTES       16384u
#define MOTORCYCLE_ADC_USB_TIMEOUT_MS        5000u
#define MOTORCYCLE_ADC_USB_TIMEOUT_RETRIES   6u
/* The CDC driver has eight 512-byte IN requests.  Never enter its infinite
 * waiter when Windows stops completing them: a 512-byte nonblocking write
 * plus a bounded retry keeps this task responsive to a USB re-enumeration. */
#define MOTORCYCLE_POINT_USB_CHUNK_BYTES     512u
#define MOTORCYCLE_POINT_USB_STALL_POLLS     1000u

struct __attribute__((packed)) motorcycle_header {
    uint16_t magic_word[4];
    uint32_t version;
    uint32_t total_packet_length;
    uint8_t platform;
    uint8_t reserved_0[3];
    uint32_t frame_number;
    uint8_t number_of_tlvs;
    uint8_t reserved_1[3];
};

struct __attribute__((packed)) motorcycle_tlv_header {
    uint32_t type;
    uint32_t length;
};

struct __attribute__((packed)) motorcycle_detection_header {
    uint16_t number_of_detections;
    uint16_t reserved;
};

/* Exact NEW_PROTOCOL_ENABLE MotorCycle_DetObj wire layout. */
struct __attribute__((packed)) motorcycle_detection_wire {
    uint16_t rel_rd_index;
    uint8_t motion_state;
    uint8_t is_peak;
    float velocity_mps;
    float x_output_m;
    float y_output_m;
    float range_m;
    float azimuth_deg;
    int16_t velocity_ambiguous_x100;
    int16_t x_rcs_x100;
    int16_t y_rcs_x100;
    uint8_t velocity_disamb_confidence;
    uint8_t velocity_disamb_factor;
    uint16_t power_x100;
    uint16_t snr_x100;
    uint32_t reserved;
};

struct __attribute__((packed)) motorcycle_track_header {
    uint8_t reserved[3];
    uint8_t number_of_tracks;
    uint32_t measurement_counter;
};

/* Explicit padding reproduces the natural-alignment NEW_PROTOCOL_ENABLE
 * MotorCycle_TrkObj ABI without leaking uninitialised compiler padding. */
struct __attribute__((packed)) motorcycle_track_wire {
    uint8_t track_id;
    uint8_t alignment_pad;
    uint16_t x_output;
    uint16_t y_output;
    uint16_t vx_output;
    uint16_t vy_output;
    uint8_t motion_state;
    uint8_t is_valid;
    uint8_t max_length_x10;
    uint8_t max_width_x10;
    uint16_t heading_output;
    uint8_t reserved;
    uint8_t trailing_pad;
};

/* Exact 248-byte WarnResult layout used by the original 2T4R firmware. */
struct motorcycle_warning_wire {
    uint8_t active[RADAR_WARNING_CLASS_COUNT];
    uint8_t alignment_pad;
    float ttc_min_s;
    uint8_t ids_0[RADAR_WARNING_MAX_IDS]; uint8_t count_0;
    uint8_t ids_1[RADAR_WARNING_MAX_IDS]; uint8_t count_1;
    uint8_t ids_2[RADAR_WARNING_MAX_IDS]; uint8_t count_2;
    uint8_t ids_3[RADAR_WARNING_MAX_IDS]; uint8_t count_3;
    uint8_t ids_4[RADAR_WARNING_MAX_IDS]; uint8_t count_4;
    uint8_t ids_5[RADAR_WARNING_MAX_IDS]; uint8_t count_5;
    uint8_t ids_6[RADAR_WARNING_MAX_IDS]; uint8_t count_6;
    uint8_t lca_left_level;
    uint8_t lca_right_level;
    uint8_t trailing_pad[3];
};

#define MOTORCYCLE_PACKET_MAX \
    (sizeof(struct motorcycle_header) + \
     sizeof(struct motorcycle_tlv_header) + \
     sizeof(struct motorcycle_detection_header) + \
     MOTORCYCLE_OUTPUT_MAX_DETECTIONS * \
         sizeof(struct motorcycle_detection_wire) + \
     sizeof(struct motorcycle_tlv_header) + \
     sizeof(struct motorcycle_track_header) + \
     RADAR_TRACKING_MAX_TRACKS * sizeof(struct motorcycle_track_wire) + \
     sizeof(struct motorcycle_tlv_header) + \
     sizeof(struct motorcycle_warning_wire))

typedef char motorcycle_header_must_be_28_bytes[
    sizeof(struct motorcycle_header) == 28 ? 1 : -1];
typedef char motorcycle_detection_must_be_40_bytes[
    sizeof(struct motorcycle_detection_wire) == 40 ? 1 : -1];
typedef char motorcycle_track_must_be_18_bytes[
    sizeof(struct motorcycle_track_wire) == 18 ? 1 : -1];
typedef char motorcycle_warning_must_be_248_bytes[
    sizeof(struct motorcycle_warning_wire) == 248 ? 1 : -1];
typedef char motorcycle_packet_must_fit_u16[
    MOTORCYCLE_PACKET_MAX <= 0xffffu ? 1 : -1];

static const struct gadget_id motorcycle_usb_id = {
    .vendor_id = 0x0525,
    .product_id = 0xa4a7,
};

static const struct usb_cdc_serial_param motorcycle_usb_parameters = {
    .dwDTERate = RADAR_USB_CDC_LINE_CODING_BPS,
    .bCharFormat = USB_CDC_1_STOP_BITS,
    .bParityType = USB_CDC_NO_PARITY,
    .bDataBits = 8,
};

static uint8_t tx_slots[2][MOTORCYCLE_PACKET_MAX];
/* 256 detections make a packet larger than either task's stack. */
static uint8_t tx_packet[MOTORCYCLE_PACKET_MAX];
static uint8_t publish_packet[MOTORCYCLE_PACKET_MAX];
static uint16_t tx_lengths[2];
static uint32_t tx_slot_sequence[2];
static uint32_t produced_sequence;
static uint32_t consumed_sequence;
static struct motorcycle_output_stats tx_stats;
static volatile int usb_connected;
static volatile unsigned int tx_backoff_ms;
static SemaphoreHandle_t usb_tx_lock;
static char command_pending[MOTORCYCLE_COMMAND_BYTES];
/* Keep the 4 KiB line buffer off the 2 KiB USB receive task stack. */
static char command_rx_line[MOTORCYCLE_COMMAND_BYTES];
static volatile int command_ready;
static int motorcycle_send_complete(const uint8_t *packet,
                                    uint16_t packet_length);

/* A line is handed to the radar task, which owns RF and flash operations. */
static void motorcycle_rx_task(void *arg)
{
    uint8_t input[64];
    uint32_t length = 0;
    int overflow = 0;
    (void)arg;

    while (1) {
        int received = gadget_serial_read(input, sizeof(input), 1, 100);
        if (received <= 0) {
            if (received == -ENOLINK || received == -ENODEV) {
                length = 0;
                overflow = 0;
            }
            continue;
        }
        for (int i = 0; i < received; ++i) {
            uint8_t ch = input[i];
            if (ch == '\r' || ch == '\n') {
                if (length && !overflow) {
                    command_rx_line[length] = '\0';
                    taskENTER_CRITICAL();
                    if (!command_ready) {
                        memcpy(command_pending, command_rx_line, length + 1u);
                        command_ready = 1;
                    }
                    taskEXIT_CRITICAL();
                }
                length = 0;
                overflow = 0;
            } else if (ch >= 32u && ch < 127u && !overflow) {
                if (length + 1u < sizeof(command_rx_line))
                    command_rx_line[length++] = (char)ch;
                else
                    overflow = 1;
            }
        }
    }
}

int motorcycle_output_take_command(char *line, uint32_t capacity)
{
    uint32_t length;
    if (!line || !capacity)
        return -1;
    taskENTER_CRITICAL();
    if (!command_ready) {
        taskEXIT_CRITICAL();
        return 0;
    }
    length = (uint32_t)strlen(command_pending);
    if (length >= capacity) {
        command_ready = 0;
        taskEXIT_CRITICAL();
        return -1;
    }
    memcpy(line, command_pending, length + 1u);
    command_ready = 0;
    taskEXIT_CRITICAL();
    return 1;
}

int motorcycle_output_send_text(const char *message)
{
    size_t length;
    int result;
    if (!message || !usb_tx_lock || !motorcycle_output_is_connected())
        return -ENOLINK;
    length = strlen(message);
    if (length > UINT16_MAX)
        return -1;
    if (xSemaphoreTake(usb_tx_lock, pdMS_TO_TICKS(1000)) != pdTRUE)
        return -ETIMEDOUT;
    result = motorcycle_send_complete((const uint8_t *)message,
                                      (uint16_t)length);
    xSemaphoreGive(usb_tx_lock);
    return result == (int)length ? 0 : result;
}

static int motorcycle_send_complete(const uint8_t *packet,
                                    uint16_t packet_length)
{
    uint16_t total_written = 0u;
    unsigned int no_progress_polls = 0u;

    while (total_written < packet_length) {
        uint16_t remaining = packet_length - total_written;
        uint16_t request = remaining > MOTORCYCLE_POINT_USB_CHUNK_BYTES ?
                           MOTORCYCLE_POINT_USB_CHUNK_BYTES : remaining;
        int written = gadget_serial_write(packet + total_written, request,
                                          0, 0);

        if (written > 0) {
            total_written += (uint16_t)written;
            no_progress_polls = 0u;
            continue;
        }
        if (written != 0 && written != -EAGAIN)
            return written;
        if (!usb_connected || gadget_serial_get_connect_status() <= 0)
            return -ENOLINK;
        if (++no_progress_polls >= MOTORCYCLE_POINT_USB_STALL_POLLS)
            return -ETIMEDOUT;
        msleep(2);
    }
    return total_written;
}

static int motorcycle_send_large(const uint8_t *data, uint32_t length)
{
    uint32_t total_written = 0u;
    unsigned int timeout_retries = 0u;

    while (total_written < length) {
        uint32_t remaining = length - total_written;
        uint32_t request = remaining > MOTORCYCLE_ADC_USB_CHUNK_BYTES ?
                           MOTORCYCLE_ADC_USB_CHUNK_BYTES : remaining;
        int written = gadget_serial_write(data + total_written, request, 1,
                                          MOTORCYCLE_ADC_USB_TIMEOUT_MS);

        if (written > 0) {
            total_written += (uint32_t)written;
            timeout_retries = 0u;
            continue;
        }
        /* A timeout/EAGAIN queues no bytes for this call.  Retry the same
         * offset so a temporary Windows usbser stall cannot turn one ADC
         * packet into a truncated packet followed by the next frame. */
        if ((written == -ETIMEDOUT || written == -EAGAIN) &&
            ++timeout_retries < MOTORCYCLE_ADC_USB_TIMEOUT_RETRIES) {
            msleep(5);
            continue;
        }
        else
            return written;
    }
    return 0;
}

static int16_t scale_signed_x100(float value)
{
    float scaled = value * 100.0f;

    if (scaled > 32767.0f)
        return 32767;
    if (scaled < -32768.0f)
        return -32768;
    return (int16_t)(scaled >= 0.0f ? scaled + 0.5f : scaled - 0.5f);
}

static uint16_t scale_unsigned_x100(float value)
{
    float scaled = value * 100.0f;

    if (scaled <= 0.0f)
        return 0u;
    if (scaled >= 65535.0f)
        return 65535u;
    return (uint16_t)(scaled + 0.5f);
}

static uint16_t scale_offset(float value, float offset, float scale)
{
    float encoded = (value + offset) * scale;
    if (encoded <= 0.0f)
        return 0u;
    if (encoded >= 65535.0f)
        return 65535u;
    return (uint16_t)(encoded + 0.5f);
}

static uint8_t scale_unsigned_x10_u8(float value)
{
    float encoded = value * 10.0f;
    if (encoded <= 0.0f)
        return 0u;
    if (encoded >= 255.0f)
        return 255u;
    return (uint8_t)(encoded + 0.5f);
}

static void copy_warning_group(struct motorcycle_warning_wire *wire,
                               const struct motorcycle_warning *warning,
                               unsigned int type)
{
    uint8_t *ids = NULL;
    uint8_t *count = NULL;
    switch (type) {
    case 0: ids = wire->ids_0; count = &wire->count_0; break;
    case 1: ids = wire->ids_1; count = &wire->count_1; break;
    case 2: ids = wire->ids_2; count = &wire->count_2; break;
    case 3: ids = wire->ids_3; count = &wire->count_3; break;
    case 4: ids = wire->ids_4; count = &wire->count_4; break;
    case 5: ids = wire->ids_5; count = &wire->count_5; break;
    default: ids = wire->ids_6; count = &wire->count_6; break;
    }
    *count = warning->id_count[type] > RADAR_WARNING_MAX_IDS ?
             RADAR_WARNING_MAX_IDS : warning->id_count[type];
    memcpy(ids, warning->ids[type], *count);
}

static void motorcycle_connect_callback(int connected)
{
    usb_connected = connected != 0;
    tx_backoff_ms = 0;
}

static void motorcycle_serial_callback(struct usb_cdc_serial_param *parameters)
{
    (void)parameters;
}

static void motorcycle_tx_task(void *arg)
{

    (void)arg;
    while (1) {
        uint16_t packet_length = 0;

        if (tx_backoff_ms != 0u) {
            --tx_backoff_ms;
            msleep(1);
            continue;
        }
        if (usb_connected || gadget_serial_get_connect_status() > 0) {
            uint32_t sequence;
            unsigned int slot;

            taskENTER_CRITICAL();
            sequence = produced_sequence;
            slot = sequence & 1u;
            if (sequence != consumed_sequence &&
                tx_slot_sequence[slot] == sequence) {
                packet_length = tx_lengths[slot];
                memcpy(tx_packet, tx_slots[slot], packet_length);
                consumed_sequence = sequence;
            }
            taskEXIT_CRITICAL();

            if (packet_length != 0u) {
                int written = -ETIMEDOUT;
                if (xSemaphoreTake(usb_tx_lock, pdMS_TO_TICKS(1000)) == pdTRUE) {
                    written = motorcycle_send_complete(tx_packet, packet_length);
                    xSemaphoreGive(usb_tx_lock);
                }

                taskENTER_CRITICAL();
                if (written == (int)packet_length)
                {
                    ++tx_stats.sent;
                    tx_stats.consecutive_errors = 0u;
                }
                else {
                    ++tx_stats.errors;
                    tx_stats.last_error = written;
                    ++tx_stats.consecutive_errors;
                    if (written == -ETIMEDOUT) {
                        tx_backoff_ms = 5000u;
                        printf("[HOST-USB] TX stalled for 2 s; retry in 5 s, replug USB if sent stays flat\n");
                    }
                }
                taskEXIT_CRITICAL();
            }
        }
        msleep(1);
    }
}

int motorcycle_output_init(void)
{
    int result;

    usb_tx_lock = xSemaphoreCreateMutex();
    if (!usb_tx_lock)
        return -1;
    result = gadget_serial_init(&motorcycle_usb_id,
                                    &motorcycle_usb_parameters,
                                    motorcycle_connect_callback,
                                    motorcycle_serial_callback);

    if (result != 0)
        return result;
    if (thread_create("motorcycle_tx", MOTORCYCLE_TX_STACK,
                      motorcycle_tx_task, NULL) == NULL)
        return -1;
    if (thread_create("motorcycle_rx", MOTORCYCLE_RX_STACK,
                      motorcycle_rx_task, NULL) == NULL)
        return -1;
    return 0;
}

int motorcycle_output_is_connected(void)
{
    return usb_connected || gadget_serial_get_connect_status() > 0;
}

int motorcycle_output_publish_adc(uint32_t frame_id,
                                  const void *payload,
                                  uint32_t payload_bytes)
{
    uint8_t prefix[ADC_CAPTURE_PREFIX_BYTES];
    int result;

    if (!payload || payload_bytes != RADAR_PAYLOAD_BYTES ||
        adc_capture_build_prefix(prefix, sizeof(prefix), frame_id,
                                 payload_bytes) < 0)
        return -1;
    if (!motorcycle_output_is_connected()) {
        taskENTER_CRITICAL();
        ++tx_stats.dropped;
        taskEXIT_CRITICAL();
        return 1;
    }

    taskENTER_CRITICAL();
    ++tx_stats.produced;
    taskEXIT_CRITICAL();
    if (xSemaphoreTake(usb_tx_lock, pdMS_TO_TICKS(1000)) != pdTRUE)
        result = -ETIMEDOUT;
    else {
        result = motorcycle_send_large(prefix, sizeof(prefix));
        if (result == 0)
            result = motorcycle_send_large((const uint8_t *)payload,
                                           payload_bytes);
        xSemaphoreGive(usb_tx_lock);
    }
    taskENTER_CRITICAL();
    if (result == 0)
        ++tx_stats.sent;
    else
        ++tx_stats.errors;
    taskEXIT_CRITICAL();
    return result;
}

void motorcycle_output_publish(uint32_t frame_id,
                               const struct motorcycle_detection *detections,
                               uint16_t detection_count)
{
    motorcycle_output_publish_full(frame_id, detections, detection_count,
                                   NULL, 0u, NULL);
}

void motorcycle_output_publish_full(uint32_t frame_id,
                                    const struct motorcycle_detection *detections,
                                    uint16_t detection_count,
                                    const struct motorcycle_track *tracks,
                                    uint16_t track_count,
                                    const struct motorcycle_warning *warning)
{
    struct motorcycle_header header;
    struct motorcycle_tlv_header tlv;
    struct motorcycle_detection_header detection_header;
    struct motorcycle_track_header track_header;
    struct motorcycle_warning_wire warning_wire;
    uint8_t *cursor = publish_packet;
    uint32_t packet_length;
    uint32_t sequence;
    unsigned int slot;

    if (RADAR_HOST_OUTPUT_DECIMATION > 1u &&
        frame_id % RADAR_HOST_OUTPUT_DECIMATION != 0u)
        return;

    if (detections == NULL && detection_count != 0u)
        return;
    if (detection_count > MOTORCYCLE_OUTPUT_MAX_DETECTIONS)
        detection_count = MOTORCYCLE_OUTPUT_MAX_DETECTIONS;
    if (tracks == NULL)
        track_count = 0u;
    if (track_count > RADAR_TRACKING_MAX_TRACKS)
        track_count = RADAR_TRACKING_MAX_TRACKS;

    memset(&header, 0, sizeof(header));
    header.magic_word[0] = 0x0102u;
    header.magic_word[1] = 0x0304u;
    header.magic_word[2] = 0x0506u;
    header.magic_word[3] = 0x0708u;
    header.platform = MOTORCYCLE_PLATFORM;
    header.frame_number = frame_id;
    header.number_of_tlvs = 3u;
    header.total_packet_length =
        sizeof(header) + sizeof(tlv) + sizeof(detection_header) +
        detection_count * sizeof(struct motorcycle_detection_wire) +
        sizeof(tlv) + sizeof(track_header) +
        track_count * sizeof(struct motorcycle_track_wire) +
        sizeof(tlv) + sizeof(warning_wire);
    memcpy(cursor, &header, sizeof(header));
    cursor += sizeof(header);

    tlv.type = MOTORCYCLE_TLV_DETECTION_INFO;
    tlv.length = sizeof(detection_header) +
                 detection_count * sizeof(struct motorcycle_detection_wire);
    memcpy(cursor, &tlv, sizeof(tlv));
    cursor += sizeof(tlv);
    detection_header.number_of_detections = detection_count;
    detection_header.reserved = 0u;
    memcpy(cursor, &detection_header, sizeof(detection_header));
    cursor += sizeof(detection_header);

    for (unsigned int index = 0; index < detection_count; ++index) {
        struct motorcycle_detection_wire wire;
        const struct motorcycle_detection *source = &detections[index];

        memset(&wire, 0, sizeof(wire));
        wire.rel_rd_index = source->rel_rd_index;
        wire.motion_state = source->motion_state;
        wire.is_peak = source->is_peak;
        wire.velocity_mps = source->velocity_mps;
        wire.x_output_m = source->x_output_m;
        wire.y_output_m = source->y_output_m;
        wire.range_m = source->range_m;
        wire.azimuth_deg = source->azimuth_deg;
        wire.velocity_ambiguous_x100 =
            scale_signed_x100(source->velocity_ambiguous_mps);
        wire.x_rcs_x100 = scale_signed_x100(source->x_rcs_m);
        wire.y_rcs_x100 = scale_signed_x100(source->y_rcs_m);
        wire.velocity_disamb_confidence =
            source->velocity_disamb_confidence;
        wire.velocity_disamb_factor = source->velocity_disamb_factor;
        wire.power_x100 = scale_unsigned_x100(source->power);
        wire.snr_x100 = scale_unsigned_x100(source->snr);
        memcpy(cursor, &wire, sizeof(wire));
        cursor += sizeof(wire);
    }

    tlv.type = MOTORCYCLE_TLV_TRACK_INFO;
    tlv.length = sizeof(track_header) +
                 track_count * sizeof(struct motorcycle_track_wire);
    memcpy(cursor, &tlv, sizeof(tlv));
    cursor += sizeof(tlv);
    memset(&track_header, 0, sizeof(track_header));
    track_header.number_of_tracks = (uint8_t)track_count;
    track_header.measurement_counter = frame_id;
    memcpy(cursor, &track_header, sizeof(track_header));
    cursor += sizeof(track_header);

    for (unsigned int index = 0; index < track_count; ++index) {
        struct motorcycle_track_wire wire;
        float heading = tracks[index].heading_deg;
        memset(&wire, 0, sizeof(wire));
        while (heading > 180.0f) heading -= 360.0f;
        while (heading < -180.0f) heading += 360.0f;
        wire.track_id = tracks[index].track_id;
        wire.x_output = scale_offset(tracks[index].x_m, 255.0f, 8.0f);
        wire.y_output = scale_offset(tracks[index].y_m, 255.0f, 8.0f);
        wire.vx_output = scale_offset(tracks[index].vx_mps, 102.0f, 20.0f);
        wire.vy_output = scale_offset(tracks[index].vy_mps, 102.0f, 20.0f);
        wire.motion_state = tracks[index].motion_state;
        wire.is_valid = tracks[index].valid;
        wire.max_length_x10 = scale_unsigned_x10_u8(tracks[index].length_m);
        wire.max_width_x10 = scale_unsigned_x10_u8(tracks[index].width_m);
        wire.heading_output = scale_offset(heading, 180.0f, 2.5f);
        memcpy(cursor, &wire, sizeof(wire));
        cursor += sizeof(wire);
    }

    memset(&warning_wire, 0, sizeof(warning_wire));
    warning_wire.ttc_min_s = 999.0f;
    if (warning) {
        memcpy(warning_wire.active, warning->active,
               RADAR_WARNING_CLASS_COUNT);
        warning_wire.ttc_min_s = warning->ttc_min_s;
        for (unsigned int type = 0; type < RADAR_WARNING_CLASS_COUNT; ++type)
            copy_warning_group(&warning_wire, warning, type);
        warning_wire.lca_left_level = warning->lca_left_level;
        warning_wire.lca_right_level = warning->lca_right_level;
    }
    tlv.type = MOTORCYCLE_TLV_WARNING_INFO;
    tlv.length = sizeof(warning_wire);
    memcpy(cursor, &tlv, sizeof(tlv));
    cursor += sizeof(tlv);
    memcpy(cursor, &warning_wire, sizeof(warning_wire));
    cursor += sizeof(warning_wire);

    packet_length = (uint32_t)(cursor - publish_packet);
    taskENTER_CRITICAL();
    if (produced_sequence != consumed_sequence)
        ++tx_stats.dropped;
    sequence = produced_sequence + 1u;
    slot = sequence & 1u;
    memcpy(tx_slots[slot], publish_packet, packet_length);
    tx_lengths[slot] = (uint16_t)packet_length;
    tx_slot_sequence[slot] = sequence;
    produced_sequence = sequence;
    ++tx_stats.produced;
    taskEXIT_CRITICAL();
}

void motorcycle_output_get_stats(struct motorcycle_output_stats *stats)
{
    uint32_t connected;

    if (stats == NULL)
        return;
    connected = usb_connected || gadget_serial_get_connect_status() > 0;
    taskENTER_CRITICAL();
    *stats = tx_stats;
    stats->connected = connected;
    taskEXIT_CRITICAL();
}
