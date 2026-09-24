#include "eol_calibration.h"
#include "../config_manager.h"
#include <math.h>
#include <stdio.h>
#include "../motor_cycle_demo/inc/general_functions.h"

extern volatile int g_is_eol_mode;
uint8_t g_calib_operation = 0;     // 0: Stop, 1: Start
uint8_t g_calib_result_status = 0xFF; // 0: Correct, 1-8: Errors, 0xFF: Not Calibrated

static int frame_count = 0;
static int match_count = 0;
static float angle_sum = 0.0f;
static int timeout_frames = 0;
static uint8_t s_closest_err = 1; // 1: Not found (default)
static uint64_t s_calib_start_time_ns = 0;

// Cached calibration parameters
static float s_target_dist = 0.0f;
static float s_target_vel = 0.0f;
static float s_target_ang = 0.0f;
static float s_pwr_thres = 0.0f;

void eol_calib_start(void) {
    // Read all parameters once
    param_get(PARAM_CALIB_TARGET_DIST, &s_target_dist, sizeof(float));
    param_get(PARAM_CALIB_TARGET_VEL, &s_target_vel, sizeof(float));
    param_get(PARAM_CALIB_TARGET_ANG, &s_target_ang, sizeof(float));
    param_get(PARAM_CALIB_POWER_TH, &s_pwr_thres, sizeof(float));

    printf("[EOL] param_get() results -> dist: %.2f, vel: %.2f, ang: %.2f, pwr_th: %.2f\n", 
           s_target_dist, s_target_vel, s_target_ang, s_pwr_thres);

    if (s_target_dist <= 0.0f) {
        printf("[EOL] Calib target distance invalid (%.2f), aborting start.\n", s_target_dist);
        g_calib_result_status = 2; // 2: Equipment range error / Missing config
        param_set(PARAM_CALIB_RESULT, &g_calib_result_status, sizeof(uint8_t));
        g_calib_operation = 0;
        return;
    }

    frame_count = 0;
    match_count = 0;
    angle_sum = 0.0f;
    timeout_frames = 0;
    s_closest_err = 1;

    g_calib_operation = 1;
    g_calib_result_status = 8; // 8: Processing
    
    s_calib_start_time_ns = get_time_ns();
}


void eol_calib_stop(void) {
    g_calib_operation = 0;
    g_is_eol_mode = 0; // Resume normal point cloud transmission
    
    uint64_t end_time_ns = get_time_ns();
    float total_time_ms = (end_time_ns - s_calib_start_time_ns) / 1000000.0f;
    
    printf("[EOL] Calibration stopped. Total elapsed time: %.2f ms\n", total_time_ms);
}

void eol_calib_process_frame(DetInfo* detInfo) {
    if (g_calib_operation != 1) return;

    timeout_frames++;
    frame_count++;

    bool matched_this_frame = false;
    float current_angle_diff = 0.0f;
    
    uint8_t best_err_this_frame = 1;  // Default: 1 (Nothing found anywhere)
    bool found_static_point_anywhere = false;

    printf("[EOL-DEBUG] Frame %d: numDets = %d\n", timeout_frames, detInfo->numDets);

    for (int i = 0; i < detInfo->numDets; i++) {
        DetObj* pt = &detInfo->detObj[i];
        
        printf("[EOL-DEBUG]   -> Pt %d: Dist: %.2f m, Ang: %.2f deg, SNR: %.2f dB, Vel: %.2f m/s\n", 
               i, pt->x_output, pt->azm_deg, pt->snr, pt->vlc);
        
        float diff_vel = fabsf(pt->vlc - s_target_vel);
        if (diff_vel >= EOL_CALIB_TOL_VEL) {
            continue; // Ignore moving noise (not our static calibration target)
        }
        
        found_static_point_anywhere = true; // Found at least one valid static candidate
        
        // Use x_output (longitudinal distance) instead of raw radial range (rng)
        float diff_dist = fabsf(pt->x_output - s_target_dist);
        float diff_ang = fabsf(pt->azm_deg - s_target_ang);
        
        if (diff_dist < EOL_CALIB_TOL_DIST) {
            if (diff_ang >= EOL_CALIB_TOL_ANG) {
                if (best_err_this_frame < 3) best_err_this_frame = 3; // Angle error
            } else if (pt->snr <= s_pwr_thres) {
                if (best_err_this_frame < 4) best_err_this_frame = 4; // SNR error
            } else {
                best_err_this_frame = 0; // Perfect Match
                matched_this_frame = true;
                current_angle_diff = pt->azm_deg - s_target_ang;
                break;
            }
        }
    }

    if (best_err_this_frame == 1 && found_static_point_anywhere) {
        // Did not fall into distance box, but static objects exist elsewhere
        best_err_this_frame = 2; 
    }

    // Accumulate the closest error across all frames (higher number is closer to success)
    if (best_err_this_frame != 0) {
        if (best_err_this_frame > s_closest_err) {
            s_closest_err = best_err_this_frame;
        }
    }

    if (matched_this_frame) {
        match_count++;
        angle_sum += current_angle_diff;
    }

    // Block evaluation
    if (frame_count >= EOL_CALIB_MATCH_WINDOW) {
        if (match_count >= EOL_CALIB_MATCH_REQUIRED) {
            // Success
            float avg_angle = angle_sum / match_count;
            printf("[EOL] SUCCESS! Avg Angle Diff: %.2f deg (from %d matches)\n", avg_angle, match_count);
            
            param_set(PARAM_CALIB_RESULT_ANG, &avg_angle, sizeof(float));
            g_calib_result_status = 0; // 0: Correct
            param_set(PARAM_CALIB_RESULT, &g_calib_result_status, sizeof(uint8_t));
            
            eol_calib_stop();
            return;
        } else {
            // Block failed, restart block
            frame_count = 0;
            match_count = 0;
            angle_sum = 0.0f;
        }
    }

    // Global timeout check
    if (timeout_frames >= EOL_CALIB_TIMEOUT_FRAMES) {
        printf("[EOL] TIMEOUT (%d frames). Reporting closest error code: %d\n", timeout_frames, s_closest_err);
        g_calib_result_status = s_closest_err; // Report the best specific error (1, 2, 3, or 4) instead of 6
        param_set(PARAM_CALIB_RESULT, &g_calib_result_status, sizeof(uint8_t));
        eol_calib_stop();
    }
}

int eol_uds_process_request(const uint8_t *req_data, uint8_t req_len, 
                            uint8_t *resp_data, uint8_t *resp_len)
{
    if (!req_data || !resp_data || !resp_len || req_len < 3) return -1;

    // Mute point cloud transmission when EOL diagnostics are active
    g_is_eol_mode = 1;

    uint8_t rw_flag = (req_data[0] >> 4) & 0x0F;
    uint8_t data_len = req_data[0] & 0x0F;
    uint16_t did = (req_data[1] << 8) | req_data[2];
    
    const uint8_t *payload = &req_data[3];
    uint8_t payload_len = req_len > 3 ? req_len - 3 : 0;
    if (data_len < payload_len) payload_len = data_len;
    
    uint8_t status = 0; // 0: Success
    
    printf("[EOL] UDS RX -> DID: 0x%04X, Type: %s, Payload Len: %d\n", 
           did, rw_flag == 1 ? "Write" : "Read", payload_len);
    
    if (rw_flag == 1) { // Write Request
        ParamID param_id = PARAM_MAX_COUNT;
        
        switch (did) {
            case DID_SERIAL_NUMBER:     param_id = PARAM_SERIAL_NUMBER; break;
            case DID_PRODUCTION_DATE:   param_id = PARAM_PRODUCTION_DATE; break;
            case DID_CALIB_TARGET_DIST: param_id = PARAM_CALIB_TARGET_DIST; break;
            case DID_CALIB_TARGET_ANG:  param_id = PARAM_CALIB_TARGET_ANG; break;
            case DID_CALIB_POWER_TH:    param_id = PARAM_CALIB_POWER_TH; break;
            case DID_CALIB_TARGET_VEL:  param_id = PARAM_CALIB_TARGET_VEL; break;
            case DID_CALIB_OPERATION:
                if (payload_len >= 1) {
                    if (payload[0] == 1) {
                        eol_calib_start();
                    } else {
                        eol_calib_stop();
                    }
                } else {
                    status = 1;
                }
                break;
            default:
                status = 1; // Unsupported or Read-Only DID
                break;
        }
        
        if (param_id != PARAM_MAX_COUNT) {
            if (param_set(param_id, payload, payload_len) != 0) {
                status = 1; // Write failed
            } else {
                printf("[EOL] Param Set Success! DID: 0x%04X\n", did);
            }
        }
        
        resp_data[0] = (1 << 4) | 3;
        resp_data[1] = (did >> 8) & 0xFF;
        resp_data[2] = did & 0xFF;
        resp_data[3] = status;
        *resp_len = 4;
        
    } else if (rw_flag == 0) { // Read Request
        ParamID param_id = PARAM_MAX_COUNT;
        uint8_t read_buf[8] = {0};
        uint8_t read_len = 0;
        
        switch (did) {
            case DID_BOOTLOADER_VERSION:
                read_buf[0]=1; read_buf[1]=0; read_buf[2]=0; read_buf[3]=0; read_buf[4]=0;
                read_len = 5;
                break;
            case DID_APP_SOFTWARE_VERSION:
                read_buf[0]=1; read_buf[1]=0; read_buf[2]=1; read_buf[3]=0; read_buf[4]=0;
                read_len = 5;
                break;
            case DID_SERIAL_NUMBER:     param_id = PARAM_SERIAL_NUMBER; read_len = 5; break;
            case DID_PRODUCTION_DATE:   param_id = PARAM_PRODUCTION_DATE; read_len = 4; break;
            case DID_CALIB_TARGET_DIST: param_id = PARAM_CALIB_TARGET_DIST; read_len = 4; break;
            case DID_CALIB_TARGET_ANG:  param_id = PARAM_CALIB_TARGET_ANG; read_len = 4; break;
            case DID_CALIB_POWER_TH:    param_id = PARAM_CALIB_POWER_TH; read_len = 4; break;
            case DID_CALIB_TARGET_VEL:  param_id = PARAM_CALIB_TARGET_VEL; read_len = 4; break;
            case DID_CALIB_RESULT_ANG:  param_id = PARAM_CALIB_RESULT_ANG; read_len = 4; break;
            case DID_CALIB_OPERATION:
                read_buf[0] = g_calib_operation;
                read_len = 1;
                break;
            case DID_CALIB_RESULT_STATUS:
                read_buf[0] = g_calib_result_status;
                read_len = 1;
                break;
            default:
                break;
        }
        
        if (param_id != PARAM_MAX_COUNT) {
            if (param_get(param_id, read_buf, read_len) != 0) {
                read_len = 0; // Read failed
            }
        }
        
        uint8_t report_len = 2 + read_len;
        if (report_len > 7) report_len = 7;
        
        resp_data[0] = (0 << 4) | report_len;
        resp_data[1] = (did >> 8) & 0xFF;
        resp_data[2] = did & 0xFF;
        if (read_len > 0) {
            memcpy(&resp_data[3], read_buf, read_len);
        }
        *resp_len = 3 + read_len;
        if (*resp_len > 8) *resp_len = 8;
    }
    
    return 0;
}
