#ifndef _FISHEYE_CALIBRATION_H_
#define _FISHEYE_CALIBRATION_H_

#include <types.h>
#include <error-base.h>
#include <math.h>

struct fc_main
{
	u32 input_width;
	u32 input_height;

	u32 output_width;
	u32 output_height;

	u32 focus_src_x;
	u32 focus_src_y;

	u32 focus_dst_x;
	u32 focus_dst_y;

	float calibration_params[2];
	float calibration_ratio;

	u32* remap_matrix;
	u8* weight_x;
	u8* weight_y;
};

struct fc_lookdown
{
	u32 input_width;
	u32 input_height;

	u32 output_width;
	u32 output_height;

	u32 focus_src_x;
	u32 focus_src_y;

	float calibration_ratio;
	float start_radius;
	float end_radius;
	float degree_range;
	float nonlinear_param;

	u32* remap_matrix;
	u8* weight_x;
	u8* weight_y;
};

int fc_main_pretreatment_coarse(struct fc_main *config);
int fc_main_pretreatment_refined(struct fc_main *config);
int fc_main_process_coarse(struct fc_main *config, u8* input_y, u8* input_uv, u8* output_y, u8* output_uv);
int fc_main_process_refined(struct fc_main *config, u8* input_y, u8* input_uv, u8* output_y, u8* output_uv);
void fc_main_free(struct fc_main *config);

int fc_main_direct_process_coarse(struct fc_main *config, u8* input_y, u8* input_uv, u8* output_y, u8* output_uv);
int fc_main_direct_process_refined(struct fc_main *config, u8* input_y, u8* input_uv, u8* output_y, u8* output_uv);

int fc_lookdown_pretreatment_coarse(struct fc_lookdown *config);
int fc_lookdown_pretreatment_refined(struct fc_lookdown *config);
int fc_lookdown_process_coarse(struct fc_lookdown *config, u8* input_y, u8* input_uv, u8* output_y, u8* output_uv);
int fc_lookdown_process_refined(struct fc_lookdown *config, u8* input_y, u8* input_uv, u8* output_y, u8* output_uv);
void fc_lookdown_free(struct fc_lookdown *config);

int fc_lookdown_direct_process_coarse(struct fc_lookdown *config, u8* input_y, u8* input_uv, u8* output_y, u8* output_uv);
int fc_lookdown_direct_process_refined(struct fc_lookdown *config, u8* input_y, u8* input_uv, u8* output_y, u8* output_uv);

#endif /* _FISHEYE_CALIBRATION_H_ */




