#include "nalu_buf.h"
#include "nalu_buf_bs.h"
#include <errno.h>
#include <little_things.h>

// #define DEBUG_LOG_ON
#ifdef DEBUG_LOG_ON
#define DEBUG_INFO(...) printf(__VA_ARGS__);
#else
#define DEBUG_INFO(...)
#endif

/* 一些必要的错误提示打印 */
#define DEBUG_ERR(...) fprintf(stderr, __VA_ARGS__);

// 因为sps_id的取值范围为[0,31]，因此数组容量最大为32，详见7.4.2.1
static sps_t Sequence_Parameters_Set_Array[32];
// 因为pps_id的取值范围为[0,255]，因此数组容量最大为256，详见7.4.2.2
// static pps_t Picture_Parameters_Set_Array[256];

// 在rbsp_trailing_bits()之前是否有更多数据
static int more_rbsp_data(bs_t *b);

static void scaling_list(int *scalingList, int sizeOfScalingList, int *useDefaultScalingMatrixFlag, bs_t *b);
static void parse_vui_parameters(sps_t *sps, bs_t *b);
static void parse_vui_hrd_parameters(hrd_parameters_t *hrd, bs_t *b);

static void parse_pps_syntax_element(pps_t *pps, bs_t *b);
static void parse_sps_syntax_element(sps_t *sps, bs_t *b);

/**
 在rbsp_trailing_bits()之前是否有更多数据
 [h264协议文档位置]：7.2 Specification of syntax functions, categories, and descriptors
 */
static int more_rbsp_data(bs_t *b)
{
    // 0.是否已读到末尾
    if (bs_eof(b)) {return 0;}

    // 1.下一比特值是否为0，为0说明还有更多数据
    if (bs_peek_u1(b) == 0) {return 1;}

    // 2.到这说明下一比特值为1，这就要看看是否这个1就是rbsp_stop_one_bit，也即1后面是否全为0
    bs_t bs_temp;
    bs_clone(&bs_temp, b);

    // 3.跳过刚才读取的这个1，逐比特查看1后面的所有比特，直到遇到另一个1或读到结束为止
    bs_read_u1(&bs_temp);
    while(!bs_eof(&bs_temp))
    {
        if (bs_read_u1(&bs_temp) == 1) { return 1; }
    }

    return 0;
}

/**
 scaling_list函数实现
 [h264协议文档位置]：7.3.2.1.1 Scaling list syntax
 */
static void scaling_list(int *scalingList, int sizeOfScalingList, int *useDefaultScalingMatrixFlag, bs_t *b)
{
    int deltaScale;
    int lastScale = 8;
    int nextScale = 8;

    for (int j = 0; j < sizeOfScalingList; j++) {

        if (nextScale != 0) {
            deltaScale = bs_read_se(b);
            nextScale = (lastScale + deltaScale + 256) % 256;
            *useDefaultScalingMatrixFlag = (j == 0 && nextScale == 0);
        }

        scalingList[j] = (nextScale == 0) ? lastScale : nextScale;
        lastScale = scalingList[j];
    }
}

/**
 解析hrd_parameters()句法元素
 [h264协议文档位置]：Annex E.1.2
 */
static void parse_vui_hrd_parameters(hrd_parameters_t *hrd, bs_t *b)
{
    hrd->cpb_cnt_minus1 = bs_read_ue(b);
    hrd->bit_rate_scale = bs_read_u(b, 4);
    hrd->cpb_size_scale = bs_read_u(b, 4);

    for (int SchedSelIdx = 0; SchedSelIdx <= hrd->cpb_cnt_minus1; SchedSelIdx++) {
        hrd->bit_rate_value_minus1[SchedSelIdx] = bs_read_ue(b);
        hrd->cpb_size_value_minus1[SchedSelIdx] = bs_read_ue(b);
        hrd->cbr_flag[SchedSelIdx] = bs_read_u(b, 1);
    }

    hrd->initial_cpb_removal_delay_length_minus1 = bs_read_u(b, 5);
    hrd->cpb_removal_delay_length_minus1 = bs_read_u(b, 5);
    hrd->dpb_output_delay_length_minus1 = bs_read_u(b, 5);
    hrd->time_offset_length = bs_read_u(b, 5);
}

/**
 解析vui_parameters()句法元素
 [h264协议文档位置]：Annex E.1.1
 */
static void parse_vui_parameters(sps_t *sps, bs_t *b)
{
    sps->vui_parameters.aspect_ratio_info_present_flag = bs_read_u(b, 1);
    if (sps->vui_parameters.aspect_ratio_info_present_flag) {
        sps->vui_parameters.aspect_ratio_idc = bs_read_u(b, 8);
        if (sps->vui_parameters.aspect_ratio_idc == 255) { // Extended_SAR值为255
            sps->vui_parameters.sar_width = bs_read_u(b, 16);
            sps->vui_parameters.sar_height = bs_read_u(b, 16);
        }
    }

    sps->vui_parameters.overscan_info_present_flag = bs_read_u(b, 1);
    if (sps->vui_parameters.overscan_info_present_flag) {
        sps->vui_parameters.overscan_appropriate_flag = bs_read_u(b, 1);
    }

    sps->vui_parameters.video_signal_type_present_flag = bs_read_u(b, 1);
    if (sps->vui_parameters.video_signal_type_present_flag) {
        sps->vui_parameters.video_format = bs_read_u(b, 3);
        sps->vui_parameters.video_full_range_flag = bs_read_u(b, 1);

        sps->vui_parameters.colour_description_present_flag = bs_read_u(b, 1);
        if (sps->vui_parameters.colour_description_present_flag) {
            sps->vui_parameters.colour_primaries = bs_read_u(b, 8);
            sps->vui_parameters.transfer_characteristics = bs_read_u(b, 8);
            sps->vui_parameters.matrix_coefficients = bs_read_u(b, 8);
        }
    }

    sps->vui_parameters.chroma_loc_info_present_flag = bs_read_u(b, 1);
    if (sps->vui_parameters.chroma_loc_info_present_flag) {
        sps->vui_parameters.chroma_sample_loc_type_top_field = bs_read_ue(b);
        sps->vui_parameters.chroma_sample_loc_type_bottom_field = bs_read_ue(b);
    }

    sps->vui_parameters.timing_info_present_flag = bs_read_u(b, 1);
    if (sps->vui_parameters.timing_info_present_flag) {
        sps->vui_parameters.num_units_in_tick = bs_read_u(b, 32);
        sps->vui_parameters.time_scale = bs_read_u(b, 32);
        sps->vui_parameters.fixed_frame_rate_flag = bs_read_u(b, 1);
    }

    sps->vui_parameters.nal_hrd_parameters_present_flag = bs_read_u(b, 1);
    if (sps->vui_parameters.nal_hrd_parameters_present_flag) {
        parse_vui_hrd_parameters(&sps->vui_parameters.nal_hrd_parameters, b);
    }

    sps->vui_parameters.vcl_hrd_parameters_present_flag = bs_read_u(b, 1);
    if (sps->vui_parameters.vcl_hrd_parameters_present_flag) {
        parse_vui_hrd_parameters(&sps->vui_parameters.vcl_hrd_parameters, b);
    }

    if (sps->vui_parameters.nal_hrd_parameters_present_flag ||
        sps->vui_parameters.vcl_hrd_parameters_present_flag) {
        sps->vui_parameters.low_delay_hrd_flag = bs_read_u(b, 1);
    }

    sps->vui_parameters.pic_struct_present_flag = bs_read_u(b, 1);
    sps->vui_parameters.bitstream_restriction_flag = bs_read_u(b, 1);
    if (sps->vui_parameters.bitstream_restriction_flag) {
        sps->vui_parameters.motion_vectors_over_pic_boundaries_flag = bs_read_u(b, 1);
        sps->vui_parameters.max_bytes_per_pic_denom = bs_read_ue(b);
        sps->vui_parameters.max_bits_per_mb_denom = bs_read_ue(b);
        sps->vui_parameters.log2_max_mv_length_horizontal = bs_read_ue(b);
        sps->vui_parameters.log2_max_mv_length_vertical = bs_read_ue(b);
        sps->vui_parameters.max_num_reorder_frames = bs_read_ue(b);
        sps->vui_parameters.max_dec_frame_buffering = bs_read_ue(b);
    }
}

/**
 解析sps句法元素
 [h264协议文档位置]：7.3.2.1.1 Sequence parameter set data syntax
 */
static void parse_sps_syntax_element(sps_t *sps, bs_t *b)
{
    sps->profile_idc = bs_read_u(b, 8);
    sps->constraint_set0_flag = bs_read_u(b, 1);
    sps->constraint_set1_flag = bs_read_u(b, 1);
    sps->constraint_set2_flag = bs_read_u(b, 1);
    sps->constraint_set3_flag = bs_read_u(b, 1);
    sps->constraint_set4_flag = bs_read_u(b, 1);
    sps->constraint_set5_flag = bs_read_u(b, 1);
    sps->reserved_zero_2bits = bs_read_u(b, 2);
    sps->level_idc = bs_read_u(b, 8);

    sps->seq_parameter_set_id = bs_read_ue(b);

    sps->chroma_format_idc = 1; // chroma_format_idc不存在时默认为1
    if (sps->profile_idc == 100 || sps->profile_idc == 110 || sps->profile_idc == 122 || sps->profile_idc == 244 || sps->profile_idc == 44 || sps->profile_idc == 83 || sps->profile_idc == 86 || sps->profile_idc == 118 || sps->profile_idc == 128 || sps->profile_idc == 138 || sps->profile_idc == 139 || sps->profile_idc == 134 || sps->profile_idc == 135) {

        sps->chroma_format_idc = bs_read_ue(b);
        if (sps->chroma_format_idc == YUV_4_4_4) {
            sps->separate_colour_plane_flag = bs_read_u(b, 1);
        }
        sps->bit_depth_luma_minus8 = bs_read_ue(b);
        sps->bit_depth_chroma_minus8 = bs_read_ue(b);
        sps->qpprime_y_zero_transform_bypass_flag = bs_read_u(b, 1);
        sps->seq_scaling_matrix_present_flag = bs_read_u(b, 1);

        if (sps->seq_scaling_matrix_present_flag) {
            int scalingListCycle = (sps->chroma_format_idc != YUV_4_4_4) ? 8 : 12;
            for (int i = 0; i < scalingListCycle; i++) {
                sps->seq_scaling_list_present_flag[i] = bs_read_u(b, 1);
                if (sps->seq_scaling_list_present_flag[i]) {
                    if (i < 6) {
                        scaling_list(sps->ScalingList4x4[i], 16, &sps->UseDefaultScalingMatrix4x4Flag[i], b);
                    }else {
                        scaling_list(sps->ScalingList8x8[i-6], 64, &sps->UseDefaultScalingMatrix8x8Flag[i-6], b);
                    }
                }
            }
        }
    }

    sps->log2_max_frame_num_minus4 = bs_read_ue(b);
    sps->pic_order_cnt_type = bs_read_ue(b);
    if (sps->pic_order_cnt_type == 0) {
        sps->log2_max_pic_order_cnt_lsb_minus4 = bs_read_ue(b);
    }else if (sps->pic_order_cnt_type == 1) {
        sps->delta_pic_order_always_zero_flag = bs_read_u(b, 1);
        sps->offset_for_non_ref_pic = bs_read_se(b);
        sps->offset_for_top_to_bottom_field = bs_read_se(b);
        sps->num_ref_frames_in_pic_order_cnt_cycle = bs_read_ue(b);
        for (int i = 0; i < sps->num_ref_frames_in_pic_order_cnt_cycle; i++) {
            sps->offset_for_ref_frame[i] = bs_read_se(b);
        }
    }

    sps->max_num_ref_frames = bs_read_ue(b);
    sps->gaps_in_frame_num_value_allowed_flag = bs_read_u(b, 1);

    sps->pic_width_in_mbs_minus1 = bs_read_ue(b);
    sps->pic_height_in_map_units_minus1 = bs_read_ue(b);
    sps->frame_mbs_only_flag = bs_read_u(b, 1);
    if (!sps->frame_mbs_only_flag) {
        sps->mb_adaptive_frame_field_flag = bs_read_u(b, 1);
    }

    sps->direct_8x8_inference_flag = bs_read_u(b, 1);

    sps->frame_cropping_flag = bs_read_u(b, 1);
    if (sps->frame_cropping_flag) {
        sps->frame_crop_left_offset = bs_read_ue(b);
        sps->frame_crop_right_offset = bs_read_ue(b);
        sps->frame_crop_top_offset = bs_read_ue(b);
        sps->frame_crop_bottom_offset = bs_read_ue(b);
    }

    sps->vui_parameters_present_flag = bs_read_u(b, 1);
    if (sps->vui_parameters_present_flag) {
        parse_vui_parameters(sps, b);
    }
}

/**
 解析pps句法元素
 [h264协议文档位置]：7.3.2.2 Picture parameter set RBSP syntax
 */
static void parse_pps_syntax_element(pps_t *pps, bs_t *b)
{
    // 解析slice_group_id[]需用的比特个数
    int bitsNumberOfEachSliceGroupID;

    pps->pic_parameter_set_id = bs_read_ue(b);
    pps->seq_parameter_set_id = bs_read_ue(b);
    pps->entropy_coding_mode_flag = bs_read_u(b, 1);
    pps->bottom_field_pic_order_in_frame_present_flag = bs_read_u(b, 1);

    /*  —————————— FMO相关 Start  —————————— */
    pps->num_slice_groups_minus1 = bs_read_ue(b);
    if (pps->num_slice_groups_minus1 > 0) {
        pps->slice_group_map_type = bs_read_ue(b);
        if (pps->slice_group_map_type == 0)
        {
            for (int i = 0; i <= pps->num_slice_groups_minus1; i++) {
                pps->run_length_minus1[i] = bs_read_ue(b);
            }
        }
        else if (pps->slice_group_map_type == 2)
        {
            for (int i = 0; i < pps->num_slice_groups_minus1; i++) {
                pps->top_left[i] = bs_read_ue(b);
                pps->bottom_right[i] = bs_read_ue(b);
            }
        }
        else if (pps->slice_group_map_type == 3 ||
                 pps->slice_group_map_type == 4 ||
                 pps->slice_group_map_type == 5)
        {
            pps->slice_group_change_direction_flag = bs_read_u(b, 1);
            pps->slice_group_change_rate_minus1 = bs_read_ue(b);
        }
        else if (pps->slice_group_map_type == 6)
        {
            // 1.计算解析slice_group_id[]需用的比特个数，Ceil( Log2( num_slice_groups_minus1 + 1 ) )
            if (pps->num_slice_groups_minus1+1 >4)
                bitsNumberOfEachSliceGroupID = 3;
            else if (pps->num_slice_groups_minus1+1 > 2)
                bitsNumberOfEachSliceGroupID = 2;
            else
                bitsNumberOfEachSliceGroupID = 1;

            // 2.动态初始化指针pps->slice_group_id
            pps->pic_size_in_map_units_minus1 = bs_read_ue(b);
            pps->slice_group_id = calloc(pps->pic_size_in_map_units_minus1+1, 1);
            if (pps->slice_group_id == NULL) {
                fprintf(stderr, "%s\n", "parse_pps_syntax_element slice_group_id Error");
                exit(-1);
            }

            for (int i = 0; i <= pps->pic_size_in_map_units_minus1; i++) {
                pps->slice_group_id[i] = bs_read_u(b, bitsNumberOfEachSliceGroupID);
            }
        }
    }
    /*  —————————— FMO相关 End  —————————— */

    pps->num_ref_idx_l0_default_active_minus1 = bs_read_ue(b);
    pps->num_ref_idx_l1_default_active_minus1 = bs_read_ue(b);

    pps->weighted_pred_flag = bs_read_u(b, 1);
    pps->weighted_bipred_idc = bs_read_u(b, 2);

    pps->pic_init_qp_minus26 = bs_read_se(b);
    pps->pic_init_qs_minus26 = bs_read_se(b);
    pps->chroma_qp_index_offset = bs_read_se(b);

    pps->deblocking_filter_control_present_flag = bs_read_u(b, 1);
    pps->constrained_intra_pred_flag = bs_read_u(b, 1);
    pps->redundant_pic_cnt_present_flag = bs_read_u(b, 1);

    // 如果有更多rbsp数据
    if (more_rbsp_data(b)) {
        pps->transform_8x8_mode_flag = bs_read_u(b, 1);
        pps->pic_scaling_matrix_present_flag = bs_read_u(b, 1);
        if (pps->pic_scaling_matrix_present_flag) {
            int chroma_format_idc = Sequence_Parameters_Set_Array[pps->seq_parameter_set_id].chroma_format_idc;
            int scalingListCycle = 6 + ((chroma_format_idc != YUV_4_4_4) ? 2 : 6) * pps->transform_8x8_mode_flag;
            for (int i = 0; i < scalingListCycle; i++) {
                pps->pic_scaling_list_present_flag[i] = bs_read_u(b, 1);
                if (pps->pic_scaling_list_present_flag[i]) {
                    if (i < 6) {
                        scaling_list(pps->ScalingList4x4[i], 16, &pps->UseDefaultScalingMatrix4x4Flag[i], b);
                    }else {
                        scaling_list(pps->ScalingList8x8[i-6], 64, &pps->UseDefaultScalingMatrix8x8Flag[i], b);
                    }
                }
            }
        }
        pps->second_chroma_qp_index_offset = bs_read_se(b);
    }else {
        pps->second_chroma_qp_index_offset = pps->chroma_qp_index_offset;
    }
}

void nalu_sps_parse_wh(const sps_t *sps, int *width, int *height) {
    *width = (sps->pic_width_in_mbs_minus1 + 1) * 16;
    *height = (sps->pic_height_in_map_units_minus1 + 1) * 16;

    if(sps->frame_cropping_flag) {
        unsigned int crop_unit_x;
        unsigned int crop_unit_y;
        switch (sps->chroma_format_idc)
        {
        case YUV_4_0_0:
            crop_unit_x = 1;
            crop_unit_y = 2 - sps->frame_mbs_only_flag;
            break;
        case YUV_4_2_0:
            crop_unit_x = 2;
            crop_unit_y = 2 * (2 - sps->frame_mbs_only_flag);
            break;
        case YUV_4_2_2:
            crop_unit_x = 2;
            crop_unit_y = 2 - sps->frame_mbs_only_flag;
            break;
        case YUV_4_4_4:
            crop_unit_x = 1;
            crop_unit_y = 2 - sps->frame_mbs_only_flag;
            break;
        default:
            DEBUG_ERR("Invalid sps!!!\n");
            return;
        }
        *width -= crop_unit_x * (sps->frame_crop_left_offset + sps->frame_crop_right_offset);
        *height -= crop_unit_y * (sps->frame_crop_top_offset + sps->frame_crop_bottom_offset);
    }
}

int nalu_sps_parse_fps(const sps_t *sps) {
    if(sps->vui_parameters_present_flag && sps->vui_parameters.timing_info_present_flag) {
        int fps = sps->vui_parameters.time_scale / sps->vui_parameters.num_units_in_tick;
        fps /= 2;
        return fps;
    } else {
        return 0;
    }
}

/*************************************改写的相关解析函数开始*******************************************/

/**
 * 根据传入的nal_data和nal_lookback_data剔除防竞争字段内容
 * 返回剔除防竞争字段内容的长度，小于零为失败
 * @nal_data:nalu_unit的数据内容起始位置
 * @nal_data_len:nalu_unit的数据内容长度
 * @rbsp_buf:剔除防竞争字段后的内容存储空间
 * @rbsp_len:剔除防竞争字段后的内容存储空间长度
*/
static int nal_to_rbsp(const uint8_t *nal_data, int nal_data_len, uint8_t *rbsp_buf, int rbsp_len)
{
    int i;
    int j = 0;
    int count = 0;

    if (!nal_data || nal_data_len <= 0) {
        DEBUG_ERR("nalu data is invalid!\n");
        return -EINVAL;
    }

    // 先将直接的数据内容 nal_data检查
    for(i = 0; i < nal_data_len; i++) {
        // NAL中不可能出现0x000000/0x000001/0x000002
        if(count == 2 && nal_data[i] < 0x03) {
            return -EPROTO;
        }

        if(count == 2 && nal_data[i] == 0x03) {
            // 0x000003之后只能出现00/01/02/03
            if((i < nal_data_len - 1) && (nal_data[i+1] > 0x03)) {
                return -EPROTO;
            }

            // 在使用了cabac_zero_word的情况下，最后3字节是0x000003时，去掉最后一字节的0x3
            if(i == nal_data_len - 1) {
                break;
            }
            i++;
            count = 0;
        }

        if(j >= rbsp_len) {
            // rbsp_buf空间不足
            DEBUG_ERR("malloc rbsp buf len too small!\n");
            return -ENOMEM;
        }
        rbsp_buf[j] = nal_data[i];
        if(nal_data[i] == 0x00) {
            count++;
        } else {
            count = 0;
        }
        j++;
    }

    return j;
}

/**
 * 剔除防竞争字，并解析该 unit 的更多相关信息，目前仅支持解析 SPS 和 PPS类型的 nalu_unit
 * 解析成功返回0，失败返回其他值
 * @nal:存储剔除防竞争字后的相关信息
 * @data: 数据内容，即以 001或 0001开头的数据内容
 * @data_len: 数据内容长度
 * @rbsp_buf: 外部申请的存储剔除防竞争字后的数据内容
 * @rbsp_buf_len: 外部申请的 rbsp_buf长度
*/
int nalu_data_parse(nal_t *nal, const uint8_t *data, int data_len, uint8_t *rbsp_buf, int rbsp_buf_len)
{
    int len = 0;
    uint8_t start_code_len = 3;

    if (!nal || !data || data_len <= 0 || !rbsp_buf || rbsp_buf_len <=0) {
        DEBUG_ERR("Invalid argument!\n");
        return -EINVAL;
    }

    if (data_len > rbsp_buf_len) {
        DEBUG_ERR("the size of data is bigger than rbsp_buf_len!\n");
        return -ENOMEM;
    }

    /* 区分001 0001，便于后续剔除防竞争字节 */
    if (data[3] == 1)
        start_code_len = 4;

    // 去除防竞争字节
    len = nal_to_rbsp(data+start_code_len, data_len-start_code_len, rbsp_buf, rbsp_buf_len);
    if(len < 0) {
        DEBUG_ERR("nal_to_rbsp fail! ret[%d]\n", len);
        return -EIO;
    }

    bs_t *b = bs_new(rbsp_buf, len);
    if (!b) {
        DEBUG_ERR("%s: bs_new fail!\n", __func__);
        return -ENOMEM;
    }
    bs_skip_u(b, 1); // forbidden_zero_bit
    nal->nal_ref_idc = bs_read_u(b, 2);
    nal->nal_unit_type = bs_read_u(b, 5);

    switch (nal->nal_unit_type)
    {
    case NALU_TYPE_SPS:
        parse_sps_syntax_element(&nal->sps, b);
        break;
    case NALU_TYPE_PPS:
        parse_pps_syntax_element(&nal->pps, b);
        break;
    default:
        break;
    }
    bs_free(b);

    return 0;
}

/*************************************改写的相关解析函数结束*******************************************/

/***************************************环形缓冲相关实现开始*******************************************/

struct nalu_buf *nalu_buf_init(int size)
{
    if (size <= 0)
        return NULL;

    struct nalu_buf *nalu_buf = malloc(sizeof(struct nalu_buf));
    if (!nalu_buf)
        return NULL;
    memset(nalu_buf, 0, sizeof(struct nalu_buf));

    /* nalu_buf mem size must be a power of 2 */
    size = roundup_pow_of_two(size);
    DEBUG_INFO("buf sz set to %d\n", size);

    unsigned char *mem = (unsigned char *)malloc(size);
    if (!mem) {
        free(nalu_buf);
        return NULL;
    }

    nalu_buf->mem_addr = mem;
    nalu_buf->mem_size = size;
    nalu_buf->write_pos = 0;
    nalu_buf->parse_pos = 0;
    nalu_buf->read_pos = 0;

    nalu_buf->start_code_addr = NULL;
    nalu_buf->start_code_len = 0;
    nalu_buf->matching_state = 0;

    return nalu_buf;
}

void nalu_buf_deinit(struct nalu_buf *nalu_buf)
{
    if (!nalu_buf)
        return;

    if (nalu_buf->mem_addr) {
        free(nalu_buf->mem_addr);
        nalu_buf->mem_addr = NULL;
    }

    free(nalu_buf);
}

/* 可以继续解析查找开始码的数据内容长度 */
static unsigned int nalu_buf_parseable_size(struct nalu_buf *nalu_buf)
{
    return nalu_buf->write_pos - nalu_buf->parse_pos;
}

static unsigned int nalu_buf_parsed_size(struct nalu_buf *nalu_buf)
{
    return nalu_buf->parse_pos - nalu_buf->read_pos;
}

static unsigned int nalu_buf_writable_size(struct nalu_buf *nalu_buf)
{
    unsigned int len;

    /**read      parse    write
     *   |         |       |
     * ------------------------>
    * read: 为上次读出的数据结束的位置
    * parse: 为解析查找开始码处理的位置
    * write: 为写入数据的位置
    */

    /* 不可以被写覆盖的数据大小，即尚未被读取使用掉的数据段大小 */
    len = nalu_buf->write_pos - nalu_buf->read_pos;

    /* 此外的大小为可以写入的 */
    len = nalu_buf->mem_size - len;

    return len;
}

int nalu_buf_write(struct nalu_buf *nalu_buf, const void *buf, int size)
{
    if (!nalu_buf || !buf || size <= 0)
        return -EINVAL;

    void *mem = (void *)nalu_buf->mem_addr;
    unsigned int writable_size = nalu_buf_writable_size(nalu_buf);
    int pos = nalu_buf->write_pos % nalu_buf->mem_size;

    /* 缓冲已满，并且无法解析出数据，作废缓冲区所有数据，写入新数据 */
    if (!writable_size && nalu_buf->parse_pos == nalu_buf->write_pos) {
        DEBUG_ERR("no space to write! we must to drop all old data and then restart!\n");
        writable_size = nalu_buf->mem_size;

        nalu_buf->matching_state = 0;
        nalu_buf->write_pos = 0;
        nalu_buf->parse_pos = 0;
        nalu_buf->read_pos = 0;

        nalu_buf->start_code_addr = NULL;
        nalu_buf->start_code_len = 0;
    }

    if (size > writable_size)
        size = writable_size;

    if (pos + size > nalu_buf->mem_size) {
        int sz = nalu_buf->mem_size - pos;
        memcpy(mem + pos, buf, sz);
        memcpy(mem, buf + sz, size - sz);
    } else {
        memcpy(mem + pos, buf, size);
    }
    nalu_buf->write_pos += size;

    return size;
}

/**
 * 通过kmp算法进行匹配模式串 "001"
 * 返回找到的开始码结束的下一字节即001的1对于 data的 index
 * index >=0 为找到的 "001"的 1相对于 data的下标；返回 -1表示失败
*/
static int find_start_code(unsigned char *data, int data_len, uint8_t *matching_state)
{
    /* 模式串"001" */
    const char pattern[] = {0, 0, 1};

    /* 模式串匹配失败回退位置 */
    const int next[] = {0, 1, 0};

    /* 从上一次的匹配状态继续 */
    uint8_t q = *matching_state;

    int index = 0;

    while (index < data_len) {
        // 回退模式串至适合位置
        while (q > 0 && data[index] != pattern[q]) {
            q = next[q-1];
        }

        if (data[index] == pattern[q]) {
            q++;
        }

        // 0 0 1 匹配成功到 1 的位置后, 返回 1 在 data中所处的 index
        if (q == 3) {
            *matching_state = q;
            return index;
        }

        index++;
    }

    *matching_state = q;
    return -1;
}

/* 从缓冲区中读取解析一个开始码，返回开始码的位置address，以及开始码的长度start_code_len */
static int nalu_buf_parse(struct nalu_buf *nalu_buf, unsigned char **address, uint8_t *start_code_len)
{
    unsigned char *mem = (unsigned char *)nalu_buf->mem_addr;
    int parseable_size = nalu_buf_parseable_size(nalu_buf);

    int pos;
    int len = 0;
    int index = 0;
    unsigned char *data = NULL;

    while (parseable_size) {
        pos = nalu_buf->parse_pos % nalu_buf->mem_size;

        if (pos + parseable_size > nalu_buf->mem_size)
            len = nalu_buf->mem_size - pos;
        else
            len = parseable_size;

        data = mem + pos;

        index = find_start_code(data, len, &nalu_buf->matching_state);
        if (index >= 0) {
            DEBUG_INFO("%s:%d find_start_code index[%d]\n", __FILE__, __LINE__, (int)(data+index-nalu_buf->mem_addr));
            *start_code_len = 3;

            /* parse_pos 将更新到 001的下一个字节，下次解析 [parse_pos, write_pos) */
            nalu_buf->parse_pos += index + 1;

            if (nalu_buf_parsed_size(nalu_buf) >= 4) {
                int tmp = (nalu_buf->parse_pos - 4) % nalu_buf->mem_size;
                if (nalu_buf->mem_addr[tmp] == 0) {
                    *start_code_len = 4;
                    DEBUG_INFO("found 0001 index is %d\n", tmp);
                } else DEBUG_INFO("found 001 index is %d\n", tmp);
            }

            index = (nalu_buf->parse_pos - *start_code_len) % nalu_buf->mem_size;
            *address = nalu_buf->mem_addr + index;
            DEBUG_INFO("%s:%d index[%d] found data[%d %d %d %d]\n", __FILE__, __LINE__, index,
                        (*address)[0], (*address)[1], (*address)[2], (*address)[3]);
            return 0;
        }

        nalu_buf->parse_pos += len;
        parseable_size -= len;
    }

    return -1;
}

/* 从缓冲区中划分得到一个 nalu_unit */
static int nalu_buf_get_nalu_unit(struct nalu_buf *nalu_buf, struct nalu_unit *unit, unsigned char **data, unsigned int *data_len)
{
    /* 记录新划分找到的开始码地址 */
    unsigned char *new_start_code_addr = NULL;
    /* 3:001 or 4:0001 */
    uint8_t new_start_code_len = 0;

    int ret = 0;
    unsigned char tmp;

parse_again:
    /* 从 nalu_buf 里面查找解析出一个开始码，通过两个开始码来划分 nalu_unit 的开始和结束 */
    ret = nalu_buf_parse(nalu_buf, &new_start_code_addr, &new_start_code_len);
    if (ret) {
        return -1;
    }

    if (nalu_buf->start_code_addr) {
        /* 可以根据 nalu_buf->start_code_addr 和 new_start_code_addr 得到一个nalu_unit */
        if (nalu_buf->start_code_addr < new_start_code_addr) {
            /* 数据没有分段 */
            data[0] = nalu_buf->start_code_addr;
            data_len[0] = new_start_code_addr - nalu_buf->start_code_addr;

            data[1] = NULL;
            data_len[1] = 0;
        } else {
            /* 数据分段 */
            data[0] = nalu_buf->start_code_addr;
            data_len[0] = nalu_buf->mem_size - (nalu_buf->start_code_addr - nalu_buf->mem_addr);

            data[1] = nalu_buf->mem_addr;
            data_len[1] = new_start_code_addr - nalu_buf->mem_addr;
        }

        tmp = data[0][nalu_buf->start_code_len];
        unit->forbidden_bit = tmp & 0x80; //1 bit
        unit->nal_reference_idc = tmp & 0x60; // 2 bit
        unit->nal_unit_type = tmp & 0x1F; // 5bit

        /* 更新找到的开始码位置 */
        nalu_buf->start_code_addr = new_start_code_addr;
        nalu_buf->start_code_len = new_start_code_len;

        /* 更新可以被写覆盖位置 */
        nalu_buf->read_pos = nalu_buf->parse_pos - new_start_code_len;
    } else {
        /* 第一次找到起始码位置 */
        nalu_buf->start_code_addr = new_start_code_addr;
        nalu_buf->start_code_len = new_start_code_len;

        /* 更新可以被写覆盖位置 */
        nalu_buf->read_pos = nalu_buf->parse_pos - new_start_code_len;
        goto parse_again;
    }

    return 0;
}

void nalu_buf_clean(struct nalu_buf *nalu_buf)
{
    nalu_buf->write_pos = 0;
    nalu_buf->parse_pos = 0;
    nalu_buf->read_pos = 0;

    nalu_buf->start_code_addr = NULL;
    nalu_buf->start_code_len = 0;
    nalu_buf->matching_state = 0;
}

/***************************************环形缓冲相关实现结束*******************************************/

int nalu_buf_read_unit(struct nalu_buf *nalu_buf, struct nalu_unit *unit, void *buf, int buf_len)
{
    if (!nalu_buf || !unit)
        return -EINVAL;

    int ret = 0;
    unsigned char *data[2] = {0};
    unsigned int data_len[2] = {0};

    ret = nalu_buf_get_nalu_unit(nalu_buf, unit, data, data_len);
    if (ret)
        return -1;

    ret = data_len[0] + data_len[1];
    if (buf && buf_len >= ret) {
        memcpy(buf, data[0], data_len[0]);
        memcpy(buf+data_len[0], data[1], data_len[1]);
    } else {
        DEBUG_ERR("nalu unit data len[%d] bigger than buf[%p] len[%d]!\n", ret, buf, buf_len);
        ret = -EINVAL;
    }

    return ret;
}

int nalu_buf_read_tail(struct nalu_buf *nalu_buf, struct nalu_unit *unit, void *buf, int buf_len)
{
    int len = 0;
    int index = 0;
    unsigned char tmp;
    unsigned char *data[2] = {0};
    unsigned int data_len[2] = {0};
    int pos = nalu_buf->parse_pos % nalu_buf->mem_size;

    if (!nalu_buf->start_code_addr)
        return -1;

    index = nalu_buf->start_code_addr - nalu_buf->mem_addr;

    /* 可以确定 nalu_buf->start_code_addr 必然是在read 位置前，根据此计算 */
    if (index > pos) {
        /* 最后一个nalu_unit 存在环回 */
        data[0] = nalu_buf->start_code_addr;
        data_len[0] = nalu_buf->mem_size - index;

        data[1] = nalu_buf->mem_addr;
        data_len[1] = pos;
    } else {
        /* 最后一个nalu_unit 不存在环回 */
        data[0] = nalu_buf->start_code_addr;
        data_len[0] = pos - index;

        data[1] = NULL;
        data_len[1] = 0;
    }
    tmp = data[0][nalu_buf->start_code_len];
    unit->forbidden_bit = tmp & 0x80; //1 bit
    unit->nal_reference_idc = tmp & 0x60; // 2 bit
    unit->nal_unit_type = tmp & 0x1F; // 5bit

    /* 更新找到的开始码位置 */
    nalu_buf->start_code_addr = NULL;
    nalu_buf->start_code_len = 0;
    nalu_buf->matching_state = 0;

    nalu_buf->write_pos = 0;
    nalu_buf->parse_pos = 0;
    nalu_buf->read_pos = 0;

    len = data_len[0] + data_len[1];
    if (buf && len <= buf_len) {
        memcpy(buf, data[0], data_len[0]);
        memcpy(buf+data_len[0], data[1], data_len[1]);
    } else {
        DEBUG_ERR("nalu unit data len[%d] bigger than buf[%p] len[%d]!\n", len, buf, buf_len);
        len = -EINVAL;
    }

    return len;
}