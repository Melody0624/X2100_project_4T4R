/**
 * @file        detection_processing.c
 * @brief       检测处理主流水线。组织完整的信号处理流程：ADC 去直流 → Range FFT →
 *              Doppler FFT → DDMA 解码 → RD 图生成 → CFAR 检测 → 角度估计 → 后处理。
 * @author      Runze Huang
 * @company     SenardMicro
 * @date        2026-05-27
 * @version     1.0
 * @copyright   Copyright (c) 2026 SenardMicro. All rights reserved.
 */
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <float.h>
#include <math.h>
#include <complex.h>
#include <sys/time.h>
#include "radar_types.h"
#include "radar_functions.h"
#include "general_functions.h"
#include "complex_abs_f32.h"
#include "memory_pool.h"
#if USE_BPM
#include "bpm_code.h"
#endif

// 内部函数声明
static void range_fft(GlbCtx* ctx, fft_cpx_f32* adcData_winDC, fft_cpx_f32* rangeFFT_res, fft_cpx_f32* fftTemp, unsigned int NFFT_range, int pool_index);
static void doppler_fft(GlbCtx* ctx, fft_cpx_f32* rangeFFT_res, const float* winDoppler, int numRngBins, int NFFT_doppler,
	fft_cpx_f32* fftTemp, fft_cpx_f32* dopplerFFT_res, int pool_index);
static void ddma_decode(float* dopplerFFT_sumRx, unsigned int numRngBins, unsigned int numDopBinsPerSubBand,
	unsigned int numTotalSubBands, unsigned numTxs, unsigned int* maxMetricIndices, int pool_index);
static void compute_rd_map(float* dopplerFFT_sumRx, unsigned int* maxMetricIndices, unsigned int numRngBins, unsigned int numDopBinsPerSubBand,
	unsigned int numTotalSubBands, unsigned int numTxs, float* RDMap);

	// 参数提取
	unsigned int gnumTxs[MAXPOOLNUM] = {0};
	unsigned int gnumRxs[MAXPOOLNUM] = {0};
	unsigned int gadcLen[MAXPOOLNUM] = {0};
	unsigned int gnumChirps[MAXPOOLNUM] = {0};
	unsigned int gnumTotalSubBands[MAXPOOLNUM] = {0};
	unsigned int gnumVirtualAnts[MAXPOOLNUM] = {0};

	// 变量定义
	float gsampleSum[MAXPOOLNUM], gsampleMean[MAXPOOLNUM];
	// float complexMag, complexReal, complexImag;
	unsigned int gbaseIdx[MAXPOOLNUM] = {0};

	unsigned int gNFFT_range[MAXPOOLNUM] = {0};
	unsigned int gnumRngBins[MAXPOOLNUM] = {0};
	unsigned int gNFFT_doppler[MAXPOOLNUM] = {0};
	unsigned int gnumDopBinsPerSubBand[MAXPOOLNUM] = {0};
	// unsigned int totalAdcSamples = adcLen * numChirps * numRxs;

// 主函数
#if OFFLINE_DEBUG_MODE
int detection_processing(GlbCtx* ctx, fft_cpx_f32* angCalibMat, int* debug_print_counter, float** rdMapOut, int pool_index)
#else
int detection_processing(GlbCtx* ctx, fft_cpx_f32* angCalibMat, int pool_index) 
#endif
{
#if OFFLINE_DEBUG_MODE
	(void)debug_print_counter; // 抑制 C4100 未引用参数警告
#endif
	// 参数提取
	gnumTxs[pool_index] = ctx->basic_params.numTxs;
	gnumRxs[pool_index] = ctx->basic_params.numRxs;
	gadcLen[pool_index] = ctx->wave_params.adcLen;
	gnumChirps[pool_index] = ctx->wave_params.numChirps;
	gnumTotalSubBands[pool_index] = gnumTxs[pool_index] + ctx->wave_params.numEmptyBands;
	gnumVirtualAnts[pool_index] = gnumTxs[pool_index] * gnumRxs[pool_index];

	// 变量定义
	// float sampleSum, sampleMean;
	// float complexMag, complexReal, complexImag;
	// unsigned int baseIdx;

	gNFFT_range[pool_index] = nextPowerOfTwo(gadcLen[pool_index]);
	gnumRngBins[pool_index] = gNFFT_range[pool_index] / 2;
	gNFFT_doppler[pool_index] = nextPowerOfTwo(gnumChirps[pool_index]);
	gnumDopBinsPerSubBand[pool_index] = gNFFT_doppler[pool_index] / gnumTotalSubBands[pool_index];
	// unsigned int totalAdcSamples = adcLen * numChirps * numRxs;

	// 使用全局内存池
	fft_cpx_f32* adcData_winDC = g_memoryPool[pool_index].adcData_winDC;
	fft_cpx_f32* rangeFFT_res = g_memoryPool[pool_index].rangeFFT_res;
	fft_cpx_f32* dopplerFFT_res = g_memoryPool[pool_index].dopplerFFT_res;
	fft_cpx_f32* fftTemp = g_memoryPool[pool_index].fftTemp;

	// float* dopplerFFT_sumRx = g_memoryPool[pool_index].dopplerFFT_sumRx;
	// unsigned int* maxMetricIndices = g_memoryPool[pool_index].maxMetricIndices;
	// float* RDMap = g_memoryPool[pool_index].RDMap;

	// 检查内存池是否已初始化
	if (!g_memoryPool[pool_index].initialized) {
		fprintf(stderr, "detection_processing: 内存池未初始化\n");
		return -1;
	}
	if (gnumTotalSubBands[pool_index] == 6 || gnumTotalSubBands[pool_index] == 12)			// Doppler FFT点数修正
		gNFFT_doppler[pool_index] = gNFFT_doppler[pool_index] / 4 * 3;

	// ADC去直流：对每个Chirp去除其采样点的直流分量
	for (unsigned int rxIdx = 0; rxIdx < gnumRxs[pool_index]; rxIdx++) {
		for (unsigned int chirpIdx = 0; chirpIdx < gnumChirps[pool_index]; chirpIdx++) {
			gsampleSum[pool_index] = 0.0f;
			gbaseIdx[pool_index] = rxIdx * gnumChirps[pool_index] * gadcLen[pool_index] + chirpIdx * gadcLen[pool_index];
			for (unsigned int sampleIdx = 0; sampleIdx < gadcLen[pool_index]; sampleIdx++) {
				gsampleSum[pool_index] += ctx->adcData.sf1[gbaseIdx[pool_index] + sampleIdx];
			}
			gsampleMean[pool_index] = gsampleSum[pool_index] / gadcLen[pool_index];
			// 减去均值并存储到新数组（复数，虚部为0）
			for (unsigned int sampleIdx = 0; sampleIdx < gadcLen[pool_index]; sampleIdx++) {
				adcData_winDC[gbaseIdx[pool_index] + sampleIdx].r = (ctx->adcData.sf1[gbaseIdx[pool_index] + sampleIdx] - gsampleMean[pool_index]) * winRng[sampleIdx];
				adcData_winDC[gbaseIdx[pool_index] + sampleIdx].i = 0.0f;
			}
		}
	}

	// Range FFT
	range_fft(ctx, adcData_winDC, rangeFFT_res, fftTemp, gNFFT_range[pool_index], pool_index);

	// Doppler FFT
	doppler_fft(ctx, rangeFFT_res, winDop, gnumRngBins[pool_index], gNFFT_doppler[pool_index], fftTemp, dopplerFFT_res, pool_index);

	// 非相干累积（修正索引以匹配新的维度顺序）
	// 版本1（原型）
	// for (unsigned int rngIdx = 0; rngIdx < gnumRngBins[pool_index]; rngIdx++) {
	// 	for (unsigned int dopIdx = 0; dopIdx < gNFFT_doppler[pool_index]; dopIdx++) {
	// 		sampleSum = 0.0f;
	// 		for (unsigned int rxIdx = 0; rxIdx < gnumRxs[pool_index]; rxIdx++) {
	// 			complexReal = dopplerFFT_res[rxIdx * gnumRngBins[pool_index] * gNFFT_doppler[pool_index] + dopIdx * gnumRngBins[pool_index] + rngIdx].r;
	// 			complexImag = dopplerFFT_res[rxIdx * gnumRngBins[pool_index] * gNFFT_doppler[pool_index] + dopIdx * gnumRngBins[pool_index] + rngIdx].i;
	// 			complexMag = sqrtf(complexReal * complexReal + complexImag * complexImag);
	// 			sampleSum += complexMag;
	// 		}
	// 		dopplerFFT_sumRx[rngIdx + dopIdx * gnumRngBins[pool_index]] = sampleSum;
	// 	}
	// }
	// 版本2（使用MSA批量优化）
	// const int block_size = 16; // 缓存友好的块大小
	// // 使用全局内存池中的临时缓冲区
	// float **rx_abs_values = g_memoryPool[pool_index].rx_abs_values;
	// // 第一步：并行计算每个接收通道的模值（使用MSA批量优化）
	// // #pragma omp parallel for if(gnumRxs[pool_index] > 4)
	// for (int rxIdx = 0; rxIdx < gnumRxs[pool_index]; rxIdx++)
    // {
	// 	complex float *rx_data = (complex float *)dopplerFFT_res + rxIdx * gnumRngBins[pool_index] * gNFFT_doppler[pool_index];
	// 	complex_abs_f32_simd(rx_abs_values[rxIdx], rx_data, gnumRngBins[pool_index] * gNFFT_doppler[pool_index]);
	// }
	// // 第二步：按块累加所有接收通道的模值
	// // #pragma omp parallel for collapse(2) if(numRngBins * gNFFT_doppler[pool_index] > 1000)
	// for (int block_i = 0; block_i < gnumRngBins[pool_index]; block_i += block_size)
    // {
	// 	int end_i = (block_i + block_size < gnumRngBins[pool_index]) ? block_i + block_size : gnumRngBins[pool_index];

	// 	for (int block_j = 0; block_j < gNFFT_doppler[pool_index]; block_j += block_size)
    //     {
	// 		int end_j = (block_j + block_size < gNFFT_doppler[pool_index]) ? block_j + block_size : gNFFT_doppler[pool_index];
            
    //         for (int i = block_i; i < end_i; i++)
    //         {
    //             for (int j = block_j; j < end_j; j++)
    //             {
	// 				int dstoff = j * gnumRngBins[pool_index] + i;
    //                 dopplerFFT_sumRx[dstoff] = 0;
                    
    //                 // 累加所有接收通道的模值
    //                 for (int rxIdx = 0; rxIdx < gnumRxs[pool_index]; rxIdx++)
    //                 {
    //                     dopplerFFT_sumRx[dstoff] += rx_abs_values[rxIdx][dstoff];
    //                 }
    //             }
    //         }
    //     }
	// }

	// // DDMA解码
	// ddma_decode(dopplerFFT_sumRx, gnumRngBins[pool_index], gnumDopBinsPerSubBand[pool_index], gnumTotalSubBands[pool_index], gnumTxs[pool_index], maxMetricIndices, pool_index);

	// // 计算RD图
	// compute_rd_map(dopplerFFT_sumRx, maxMetricIndices, gnumRngBins[pool_index], gnumDopBinsPerSubBand[pool_index], gnumTotalSubBands[pool_index], gnumTxs[pool_index], RDMap);

	// // CFAR检测
	// cfar_detection(ctx, RDMap, gnumRngBins[pool_index], gnumDopBinsPerSubBand[pool_index], maxMetricIndices, pool_index);

	// // 角度估计
	// angle_estimation_v1(ctx, angCalibMat, winAz, dopplerFFT_res, maxMetricIndices, gnumRngBins[pool_index], gnumDopBinsPerSubBand[pool_index], gnumVirtualAnts[pool_index], pool_index);

	// // 后处理
	// detection_post_processing(ctx);

	return 0; // 成功
}
#if OFFLINE_DEBUG_MODE
int detection_processing_v2(GlbCtx* ctx, fft_cpx_f32* angCalibMat, int* debug_print_counter, float** rdMapOut, int pool_index)
#else
int detection_processing_v2(GlbCtx* ctx, fft_cpx_f32* angCalibMat, int pool_index) 
#endif
{
	
	// 安全检查：验证内存池索引
	if (pool_index < 0 || pool_index >= MAXPOOLNUM) {
		fprintf(stderr, "detection_processing_v2: 无效的内存池索引 %d\n", pool_index);
		return -1;
	}
	
	// 安全检查：验证内存池是否已初始化
	if (!g_memoryPool[pool_index].initialized) {
		fprintf(stderr, "detection_processing_v2: 内存池 %d 未初始化\n", pool_index);
		return -1;
	}

	// 使用全局内存池
	fft_cpx_f32* dopplerFFT_res = g_memoryPool[pool_index].dopplerFFT_res;

	float* dopplerFFT_sumRx = g_memoryPool[pool_index].dopplerFFT_sumRx;
	unsigned int* maxMetricIndices = g_memoryPool[pool_index].maxMetricIndices;
	float* RDMap = g_memoryPool[pool_index].RDMap;

    // printf("detection_processing_v2 pool_index: %d\n", pool_index);

	// 版本2（使用MSA批量优化）
	const int block_size = 16; // 缓存友好的块大小
	// 使用全局内存池中的临时缓冲区
	float **rx_abs_values = g_memoryPool[pool_index].rx_abs_values;
	// 第一步：并行计算每个接收通道的模值（使用MSA批量优化）
	// #pragma omp parallel for if(numRxs > 4)
	for (int rxIdx = 0; rxIdx < gnumRxs[pool_index]; rxIdx++)
    {
		complex float *rx_data = (complex float *)dopplerFFT_res + rxIdx * gnumRngBins[pool_index] * gNFFT_doppler[pool_index];
		complex_abs_f32_simd(rx_abs_values[rxIdx], rx_data, gnumRngBins[pool_index] * gNFFT_doppler[pool_index]);
	}
	// printf("第一步完成\n");
	// 第二步：按块累加所有接收通道的模值
	// #pragma omp parallel for collapse(2) if(numRngBins * NFFT_doppler > 1000)
	for (int block_i = 0; block_i < gnumRngBins[pool_index]; block_i += block_size)
    {
		int end_i = (block_i + block_size < gnumRngBins[pool_index]) ? block_i + block_size : gnumRngBins[pool_index];

		for (int block_j = 0; block_j < gNFFT_doppler[pool_index]; block_j += block_size)
        {
			int end_j = (block_j + block_size < gNFFT_doppler[pool_index]) ? block_j + block_size : gNFFT_doppler[pool_index];
            
            for (int i = block_i; i < end_i; i++)
            {
                for (int j = block_j; j < end_j; j++)
                {
					int dstoff = j * gnumRngBins[pool_index] + i;
                    dopplerFFT_sumRx[dstoff] = 0;
                    
                    // 累加所有接收通道的模值
                    for (int rxIdx = 0; rxIdx < gnumRxs[pool_index]; rxIdx++)
                    {
                        dopplerFFT_sumRx[dstoff] += rx_abs_values[rxIdx][dstoff];
                    }
                }
            }
        }
	}
	// printf("第二步完成\n");
	// DDMA解码
	ddma_decode(dopplerFFT_sumRx, gnumRngBins[pool_index], gnumDopBinsPerSubBand[pool_index], gnumTotalSubBands[pool_index], gnumTxs[pool_index], maxMetricIndices, pool_index);
	// printf("DDMA解码完成\n");
	// 计算RD图
	compute_rd_map(dopplerFFT_sumRx, maxMetricIndices, gnumRngBins[pool_index], gnumDopBinsPerSubBand[pool_index], gnumTotalSubBands[pool_index], gnumTxs[pool_index], RDMap);
	// printf("RD图计算完成\n");
	// CFAR检测
	cfar_detection(ctx, RDMap, gnumRngBins[pool_index], gnumDopBinsPerSubBand[pool_index], maxMetricIndices, pool_index);
	// printf("CFAR检测完成\n");
	// 角度估计
	angle_estimation_v1(ctx, angCalibMat, winAz, dopplerFFT_res, maxMetricIndices, gnumRngBins[pool_index], gnumDopBinsPerSubBand[pool_index], gnumVirtualAnts[pool_index], pool_index);
	// printf("角度估计完成\n");

    // int maxDopIdx = 0;
    // float* dopSum = gnumDopBinsPerSubBand[pool_index] * sizeof(float);
    // for (int dop = 0; dop < gnumDopBinsPerSubBand[pool_index]; dop++) {
    //     float sum = 0.0f;
    //     int base = dop * gnumRngBins[pool_index];  // 当前Doppler的起始内存偏移
    //     for (int rng = 0; rng < gnumRngBins[pool_index]; rng++) {
    //         sum += powf(10.0f, RDMap[base + rng] / 20.0f);
    //     }
    //     dopSum[dop] = sum;
    // }
    // float maxVal = dopSum[0];
    // for (int dop = 1; dop < gnumDopBinsPerSubBand[pool_index]; dop++) {
    //     if (dopSum[dop] > maxVal) {
    //         maxVal = dopSum[dop];
    //         maxDopIdx = dop;
    //     }
    // }
    // ctx->detInfo.detObj[0].vlc_disAmb_fac = maxDopIdx;

	return 0; // 成功
}

// Range FFT实现
static void range_fft(GlbCtx* ctx, fft_cpx_f32* adcData_winDC, fft_cpx_f32* rangeFFT_res, fft_cpx_f32* fftTemp, unsigned int NFFT_range, int pool_index) {
	// 变量定义
	unsigned int adcLen = ctx->wave_params.adcLen;
	unsigned int numChirps = ctx->wave_params.numChirps;
	unsigned int numRxs = ctx->basic_params.numRxs;
	unsigned int numRngBins = NFFT_range / 2;
	unsigned int baseIdx, outBaseIdx;

	// 使用全局内存池中的FFT配置结构体
	ne10_fft_r2c_cfg_float32_t cfg = g_memoryPool[pool_index].range_fft_cfg;
	if (cfg == NULL) {
		fprintf(stderr, "Range FFT: NE10 FFT配置未初始化\n");
		return;
	}

	// 使用全局内存池中的输入输出缓冲区
	float *in = g_memoryPool[pool_index].rangeFFT_in;
	float *out = g_memoryPool[pool_index].rangeFFT_out;
	// 检查内存池是否已初始化
	if (!g_memoryPool[pool_index].initialized)
	{
		fprintf(stderr, "range_fft: 内存池未初始化\n");
		ne10_fft_destroy_r2c_float32(cfg);
		return;
	}

	// 对每个RX和每个Chirp进行FFT
	for (unsigned int rxIdx = 0; rxIdx < numRxs; rxIdx++) {
		for (unsigned int chirpIdx = 0; chirpIdx < numChirps; chirpIdx++) {
			// 计算基索引
			baseIdx = rxIdx * numChirps * adcLen + chirpIdx * adcLen;

			// 将实数数据复制到fftTemp的实部，虚部置零
			for (unsigned int sampleIdx = 0; sampleIdx < adcLen; sampleIdx++) {
				in[sampleIdx] = adcData_winDC[baseIdx + sampleIdx].r;
			}
			// 如果adcLen < NFFT_range，填充零
			for (unsigned int rngFFTBinIdx = adcLen; rngFFTBinIdx < NFFT_range; rngFFTBinIdx++) {
				in[rngFFTBinIdx] = 0.0f;
			}

			// 执行FFT
			fft_msa(in, out, cfg);

			// 提取前numRngBins个复数到输出数组（对称性，只保留一半）
			outBaseIdx = rxIdx * numChirps * numRngBins + chirpIdx * numRngBins;
			for (unsigned int rngIdx = 0; rngIdx < numRngBins; rngIdx++) {
				rangeFFT_res[outBaseIdx + rngIdx] = (fft_cpx_f32){.r = out[2 * rngIdx], .i = out[2 * rngIdx + 1]};
				// printf("rangeFFT_res[%d] = %f + %fi\n", outBaseIdx + rngIdx, creal(rangeFFT_res[outBaseIdx + rngIdx]), cimag(rangeFFT_res[outBaseIdx + rngIdx]));
			}
		}
	}
}

// Doppler FFT实现
static void doppler_fft(GlbCtx* ctx, fft_cpx_f32* rangeFFT_res, const float* winDoppler, int numRngBins, int NFFT_doppler,
	fft_cpx_f32* fftTemp, fft_cpx_f32* dopplerFFT_res, int pool_index) {
	// 变量定义
	unsigned int numChirps = ctx->wave_params.numChirps;
	unsigned int numRxs = ctx->basic_params.numRxs;
	unsigned int baseIdx;

	// 使用全局内存池中的FFT配置结构体
	ne10_fft_r2c_cfg_float32_t cfg = g_memoryPool[pool_index].doppler_fft_cfg;
	if (cfg == NULL) {
		fprintf(stderr, "Doppler FFT: NE10 FFT配置未初始化\n");
		return;
	}

	// 使用全局内存池中的输入输出缓冲区
	// fft_cpx_f32 *in = g_memoryPool[pool_index].doppler_fft_in;
	fft_cpx_f32 *out = g_memoryPool[pool_index].doppler_fft_out;
	// 检查内存池是否已初始化
	if (!g_memoryPool[pool_index].initialized)
	{
		fprintf(stderr, "doppler_fft: 内存池未初始化\n");
		ne10_fft_destroy_r2c_float32(cfg);
		return;
	}

	// 对每个RX和每个Range Bin进行多普勒FFT
	for (int rxIdx = 0; rxIdx < numRxs; rxIdx++) {
		for (int rngIdx = 0; rngIdx < numRngBins; rngIdx++) {
			// 提取当前距离门的所有chirp数据（跨chirp维度的步长为numRngBins）

			// 计算基索引（输入索引：rxIdx * numChirps * numRngBins + chirpIdx * numRngBins + rngIdx）
			baseIdx = rxIdx * numChirps * numRngBins + rngIdx;
			for (unsigned int chirpIdx = 0; chirpIdx < numChirps; chirpIdx++) {
				// 加窗：复数乘以实数窗系数
				float win = (float)winDoppler[chirpIdx];
#if USE_BPM
				fftTemp[chirpIdx].r = rangeFFT_res[baseIdx + chirpIdx * numRngBins].r * win * (float)bpm_code[chirpIdx % 512];
				fftTemp[chirpIdx].i = rangeFFT_res[baseIdx + chirpIdx * numRngBins].i * win * (float)bpm_code[chirpIdx % 512];
#else
				fftTemp[chirpIdx].r = rangeFFT_res[baseIdx + chirpIdx * numRngBins].r * win;
				fftTemp[chirpIdx].i = rangeFFT_res[baseIdx + chirpIdx * numRngBins].i * win;
#endif
			}
			// 如果numChirps < NFFT_doppler，填充零
			for (unsigned int chirpIdx = numChirps; chirpIdx < NFFT_doppler; chirpIdx++) {
				fftTemp[chirpIdx].r = 0.0f;
				fftTemp[chirpIdx].i = 0.0f;
			}

			// 执行FFT
			ne10_fft_c2c_1d_float32_mxu_ai(out, fftTemp, cfg);

			// 存储结果到输出数组（按RX、多普勒bin、距离门顺序）
			// 输出索引：rxIdx * numRngBins * NFFT_doppler + dop * numRngBins + rng
			baseIdx = rxIdx * numRngBins * NFFT_doppler + rngIdx;
			for (int dopIdx = 0; dopIdx < NFFT_doppler; dopIdx++) {
				dopplerFFT_res[baseIdx + dopIdx * numRngBins] = out[dopIdx];
			}
		}
	}
}

static void ddma_decode(float* dopplerFFT_sumRx, unsigned int numRngBins, unsigned int numDopBinsPerSubBand,
	unsigned int numTotalSubBands, unsigned numTxs, unsigned int* maxMetricIndices, int pool_index)
{
	// 变量定义
	float* curSubBandVals = g_memoryPool[pool_index].ddma_curSubBandVals;
	float* minSubBandVal = g_memoryPool[pool_index].ddma_minSubBandVal;
	unsigned int dopIdx;
	float curMaxMag;
	
	// 检查内存池是否已初始化
	if (!g_memoryPool[pool_index].initialized) {
		fprintf(stderr, "ddma_decode: 内存池未初始化\n");
		return;
	}

	// DDMA Decode
	for (unsigned int rngIdx = 0; rngIdx < numRngBins; rngIdx++) {
		for (unsigned int subDopIdx = 0; subDopIdx < numDopBinsPerSubBand; subDopIdx++) {
			// 提取当前距离门和子带内多普勒单元的所有子带值
			for (unsigned int subBandIdx = 0; subBandIdx < numTotalSubBands; subBandIdx++) {
				dopIdx = subDopIdx + subBandIdx * numDopBinsPerSubBand;
				curSubBandVals[subBandIdx] = dopplerFFT_sumRx[dopIdx * numRngBins + rngIdx];
				if (subBandIdx < numTxs - 1)
					curSubBandVals[numTotalSubBands + subBandIdx] = dopplerFFT_sumRx[dopIdx * numRngBins + rngIdx];
			}

			// 确定每个子带窗的最小值（DDMA Metric）
			for (unsigned int subBandWinIdx = 0; subBandWinIdx < numTotalSubBands; subBandWinIdx++)
				minSubBandVal[subBandWinIdx] = array_min(curSubBandVals + subBandWinIdx, numTxs);

			// 在所有子带窗中找到最大DDMA Metric对应的子带索引
			curMaxMag = -FLT_MAX;
			for (unsigned int subBandIdx = 0; subBandIdx < numTotalSubBands; subBandIdx++)
			{
				if (minSubBandVal[subBandIdx] > curMaxMag)
				{
					curMaxMag = minSubBandVal[subBandIdx];
					maxMetricIndices[subDopIdx * numRngBins + rngIdx] = (unsigned int)subBandIdx;
				}
			}
		}
	}
}

static void compute_rd_map(float* dopplerFFT_sumRx, unsigned int* maxMetricIndices, unsigned int numRngBins, unsigned int numDopBinsPerSubBand,
	unsigned int numTotalSubBands, unsigned int numTxs, float* RDMap) {

	// 变量定义
	unsigned int dopIdx, subBandIdx;

	// RDMap 计算
	for (unsigned int rngIdx = 0; rngIdx < numRngBins; rngIdx++) {
		for (unsigned int subDopIdx = 0; subDopIdx < numDopBinsPerSubBand; subDopIdx++) {
			RDMap[subDopIdx * numRngBins + rngIdx] = 0.0F;
			for (unsigned int txIdx = 0; txIdx < numTxs; txIdx++)
			{
				subBandIdx = maxMetricIndices[subDopIdx * numRngBins + rngIdx] + txIdx;
				if (subBandIdx >= numTotalSubBands)
					subBandIdx -= numTotalSubBands;
				dopIdx = subDopIdx + subBandIdx * numDopBinsPerSubBand;
				RDMap[subDopIdx * numRngBins + rngIdx] += dopplerFFT_sumRx[dopIdx * numRngBins + rngIdx];
			}
			RDMap[subDopIdx * numRngBins + rngIdx] = 20.0f * log10f(RDMap[subDopIdx * numRngBins + rngIdx]);
		}
	}
}