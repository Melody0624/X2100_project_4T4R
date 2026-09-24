#include <stdint.h>
#include <string.h>

#include <common.h>
#include <os.h>
#include <sys/errno.h>
#include <os/freertos/include/FreeRTOS.h>
#include <os/freertos/include/task.h>
#include <usb/gadget_serial.h>

#include "motorcycle_output.h"

#define MOTORCYCLE_PLATFORM                 0x24u
#define MOTORCYCLE_TLV_DETECTION_INFO       21u
#define MOTORCYCLE_TLV_TRACK_INFO           22u
#define MOTORCYCLE_TX_STACK                 4096u

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

#define MOTORCYCLE_PACKET_MAX \
    (sizeof(struct motorcycle_header) + \
     sizeof(struct motorcycle_tlv_header) + \
     sizeof(struct motorcycle_detection_header) + \
     MOTORCYCLE_OUTPUT_MAX_DETECTIONS * \
         sizeof(struct motorcycle_detection_wire) + \
     sizeof(struct motorcycle_tlv_header) + \
     sizeof(struct motorcycle_track_header))

typedef char motorcycle_header_must_be_28_bytes[
    sizeof(struct motorcycle_header) == 28 ? 1 : -1];
typedef char motorcycle_detection_must_be_40_bytes[
    sizeof(struct motorcycle_detection_wire) == 40 ? 1 : -1];

static const struct gadget_id motorcycle_usb_id = {
    .vendor_id = 0x0525,
    .product_id = 0xa4a7,
};

static const struct usb_cdc_serial_param motorcycle_usb_parameters = {
    .dwDTERate = 115200,
    .bCharFormat = USB_CDC_1_STOP_BITS,
    .bParityType = USB_CDC_NO_PARITY,
    .bDataBits = 8,
};

static uint8_t tx_slots[2][MOTORCYCLE_PACKET_MAX];
static uint16_t tx_lengths[2];
static uint32_t tx_slot_sequence[2];
static uint32_t produced_sequence;
static uint32_t consumed_sequence;
static struct motorcycle_output_stats tx_stats;
static volatile int usb_connected;

static int motorcycle_send_complete(const uint8_t *packet,
                                    uint16_t packet_length)
{
    uint16_t total_written = 0u;

    while (total_written < packet_length) {
        int written = -1;

        for (unsigned int retry = 0; retry < 3u; ++retry) {
            written = gadget_serial_write(packet + total_written,
                                          packet_length - total_written,
                                          1, 5);
            if (written > 0)
                break;
            if (written != -ETIMEDOUT && written != -EAGAIN)
                return total_written != 0u ? total_written : written;
            if (retry + 1u < 3u)
                msleep(2);
        }
        if (written <= 0)
            return total_written != 0u ? total_written : written;
        total_written += (uint16_t)written;
        if (total_written < packet_length)
            msleep(1);
    }
    return total_written;
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

static void motorcycle_connect_callback(int connected)
{
    usb_connected = connected != 0;
}

static void motorcycle_serial_callback(struct usb_cdc_serial_param *parameters)
{
    (void)parameters;
}

static void motorcycle_tx_task(void *arg)
{
    uint8_t packet[MOTORCYCLE_PACKET_MAX];

    (void)arg;
    while (1) {
        uint16_t packet_length = 0;

        if (usb_connected || gadget_serial_get_connect_status() > 0) {
            uint32_t sequence;
            unsigned int slot;

            taskENTER_CRITICAL();
            sequence = produced_sequence;
            slot = sequence & 1u;
            if (sequence != consumed_sequence &&
                tx_slot_sequence[slot] == sequence) {
                packet_length = tx_lengths[slot];
                memcpy(packet, tx_slots[slot], packet_length);
                consumed_sequence = sequence;
            }
            taskEXIT_CRITICAL();

            if (packet_length != 0u) {
                int written = motorcycle_send_complete(packet, packet_length);

                taskENTER_CRITICAL();
                if (written == (int)packet_length)
                    ++tx_stats.sent;
                else
                    ++tx_stats.errors;
                taskEXIT_CRITICAL();
            }
        }
        msleep(1);
    }
}

int motorcycle_output_init(void)
{
    int result = gadget_serial_init(&motorcycle_usb_id,
                                    &motorcycle_usb_parameters,
                                    motorcycle_connect_callback,
                                    motorcycle_serial_callback);

    if (result != 0)
        return result;
    if (thread_create("motorcycle_tx", MOTORCYCLE_TX_STACK,
                      motorcycle_tx_task, NULL) == NULL)
        return -1;
    return 0;
}

void motorcycle_output_publish(uint32_t frame_id,
                               const struct motorcycle_detection *detections,
                               uint16_t detection_count)
{
    uint8_t packet[MOTORCYCLE_PACKET_MAX];
    struct motorcycle_header header;
    struct motorcycle_tlv_header tlv;
    struct motorcycle_detection_header detection_header;
    struct motorcycle_track_header track_header;
    uint8_t *cursor = packet;
    uint32_t packet_length;
    uint32_t sequence;
    unsigned int slot;

    if (detections == NULL && detection_count != 0u)
        return;
    if (detection_count > MOTORCYCLE_OUTPUT_MAX_DETECTIONS)
        detection_count = MOTORCYCLE_OUTPUT_MAX_DETECTIONS;

    memset(&header, 0, sizeof(header));
    header.magic_word[0] = 0x0102u;
    header.magic_word[1] = 0x0304u;
    header.magic_word[2] = 0x0506u;
    header.magic_word[3] = 0x0708u;
    header.platform = MOTORCYCLE_PLATFORM;
    header.frame_number = frame_id;
    header.number_of_tlvs = 2u;
    header.total_packet_length =
        sizeof(header) + sizeof(tlv) + sizeof(detection_header) +
        detection_count * sizeof(struct motorcycle_detection_wire) +
        sizeof(tlv) + sizeof(track_header);
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
    tlv.length = sizeof(track_header);
    memcpy(cursor, &tlv, sizeof(tlv));
    cursor += sizeof(tlv);
    memset(&track_header, 0, sizeof(track_header));
    track_header.measurement_counter = frame_id;
    memcpy(cursor, &track_header, sizeof(track_header));
    cursor += sizeof(track_header);

    packet_length = (uint32_t)(cursor - packet);
    taskENTER_CRITICAL();
    if (produced_sequence != consumed_sequence)
        ++tx_stats.dropped;
    sequence = produced_sequence + 1u;
    slot = sequence & 1u;
    memcpy(tx_slots[slot], packet, packet_length);
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
