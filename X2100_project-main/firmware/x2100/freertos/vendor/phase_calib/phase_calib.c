#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "heap_malloc.h"
#include "NE10_types.h"

#include "wave2regs_intf.h"
#include "phase_calib.h"
#include "calib_misc.h"

#define PHASE_CALIB_MAX_C2C_NFFT 512

int Phase_Shifter_IQ_LUT[2][128] =
    {
        {
            255, 255, 255, 254, 253, 252, 250, 248,
            246, 243, 240, 237, 234, 230, 226, 222,
            218, 213, 209, 204, 199, 193, 188, 182,
            176, 171, 165, 159, 152, 146, 140, 134,
            128, 121, 115, 109, 103,  96,  90,  84,
             79,  73,  67,  62,  56,  51,  46,  42,
             37,  33,  29,  25,  21,  18,  15,  12,
              9,   7,   5,   3,   2,   1,   0,   0,
              0,   0,   0,   1,   2,   3,   5,   7,
              9,  12,  15,  18,  21,  25,  29,  33,
             37,  42,  46,  51,  56,  62,  67,  73,
             79,  84,  90,  96, 103, 109, 115, 121,
            128, 134, 140, 146, 152, 159, 165, 171,
            176, 182, 188, 193, 199, 204, 209, 213,
            218, 222, 226, 230, 234, 237, 240, 243,
            246, 248, 250, 252, 253, 254, 255, 255,
        },
        {
            128, 134, 140, 146, 152, 159, 165, 171,
            176, 182, 188, 193, 199, 204, 209, 213,
            218, 222, 226, 230, 234, 237, 240, 243,
            246, 248, 250, 252, 253, 254, 255, 255,
            255, 255, 255, 254, 253, 252, 250, 248,
            246, 243, 240, 237, 234, 230, 226, 222,
            218, 213, 209, 204, 199, 193, 188, 182,
            176, 171, 165, 159, 152, 146, 140, 134,
            128, 121, 115, 109, 103,  96,  90,  84,
             79,  73,  67,  62,  56,  51,  46,  42,
             37,  33,  29,  25,  21,  18,  15,  12,
              9,   7,   5,   3,   2,   1,   0,   0,
              0,   0,   0,   1,   2,   3,   5,   7,
              9,  12,  15,  18,  21,  25,  29,  33,
             37,  42,  46,  51,  56,  62,  67,  73,
             79,  84,  90,  96, 103, 109, 115, 121,
        },
};

ne10_fft_cpx_float32_t *fft1d_func(int16_t *adc, CalibCfg_t *cfg, int fft_loops, int sfIdx)
{
    ne10_fft_cpx_float32_t *fftwin;
    ne10_fft_cpx_float32_t *fft1d;
    int nfft = CalFftNum(cfg->num_adc[sfIdx]);

    if (nfft < cfg->num_adc[sfIdx] || nfft > PHASE_CALIB_MAX_C2C_NFFT)
    {
        LOG(LOG_ERROR, "%s: invalid FFT size %d for %d ADC samples",
            __FUNCTION__, nfft, cfg->num_adc[sfIdx]);
        return 0;
    }
    fftwin = RangeAddWin_CalFft((int16_t *)adc, cfg->num_adc[sfIdx], cfg->num_chirp[sfIdx], fft_loops, nfft);
    if (fftwin == NULL)
        return NULL;
    fft1d = Fft1D_DiscardHalf(fftwin, nfft, cfg->num_chirp[sfIdx], fft_loops);
    cfg->num_range_bins = nfft / 2;
    free(fftwin);

    if (fft1d == NULL)
        return NULL;

    if (cfg->DEBUG) {
        ne10_fft_cpx_float32_t test[4][128];
        memcpy(test, fft1d, sizeof(test));
        LOG(LOG_DEBUG, "");
    }

    return fft1d;
}

void transpose2Dim(ne10_fft_cpx_float32_t *src, ne10_fft_cpx_float32_t *dst, int after_row, int after_col)
{
    for (int j = 0; j < after_col; j ++)
    {
        for (int i = 0; i < after_row; i ++)
        {
            dst[j * after_row + i] = src[i * after_col + j];
        }
    }
}


ne10_fft_cpx_float32_t *fft2d_func(ne10_fft_cpx_float32_t *fft1d, CalibCfg_t *cfg, int fft_loops,int sfIdx)
{

	int nfft;

	if (cfg->num_chirp[sfIdx] % 3 == 0)
	{
		nfft = CalFftNum(cfg->num_chirp[sfIdx] / 3) * 3;
	}
	else
	{
		nfft = CalFftNum(cfg->num_chirp[sfIdx]);
	}
	if (nfft <= 0 || nfft > PHASE_CALIB_MAX_C2C_NFFT)
		return NULL;

    ne10_fft_cpx_float32_t *fft2d = DopplerAddWin_CalFft(fft1d, cfg->num_range_bins, cfg->num_chirp[sfIdx], fft_loops, nfft);

    return fft2d;
}

double *repmat1DimDouble(double *srcBuf, int srcLen, int loops)
{
    double *dstBuf = malloc(srcLen * loops * sizeof(double));
	if (dstBuf == NULL)
		return NULL;

    for (int j = 0; j < loops; j ++)
    {
        for (int i = 0; i < srcLen; i ++)
        {
            dstBuf[j * srcLen + i] = srcBuf[i];
        }
    }
    return dstBuf;
}

int *repmat1DimInt(int *srcBuf, int srcLen, int loops)
{
    int *dstBuf = malloc(srcLen * loops * sizeof(int));
	if (dstBuf == NULL)
		return NULL;

    for (int j = 0; j < loops; j ++)
    {
        for (int i = 0; i < srcLen; i ++)
        {
            dstBuf[j * srcLen + i] = srcBuf[i];
        }
    }
    return dstBuf;
}

static double *CalSumOfFft2d(ne10_fft_cpx_float32_t *fft2d, int xsize, int ysize, int zsize)
{
    double *rdmap = malloc(xsize * ysize * sizeof(double));
    if (rdmap == NULL)
        return NULL;
    for (int i = 0; i < xsize; i ++)
    {
        for (int j = 0; j < ysize; j ++)
        {
            double val = 0;
            for (int k = 0; k < zsize; k ++)
            {
                int srcoff = Get3DimArrayOff(i, j, k, xsize, ysize, zsize);
                val += ComplexAbs(fft2d[srcoff]);
            }
            int dstoff = Get2DimArrayOff(i, j, xsize, ysize);
            rdmap[dstoff] = val;
        }
    }
    return rdmap;
}

// %% 生成误差表和IQ index表
static int phase_calib_tx_enabled(const CalibCfg_t *cfg, int tx_idx)
{
    return cfg != NULL && tx_idx >= 0 && tx_idx < cfg->N_TX &&
           (cfg->calib_tx_mask & (1U << tx_idx)) != 0U;
}

static int phase_calib_active_tx_count(const CalibCfg_t *cfg)
{
    int count = 0;

    if (cfg == NULL || cfg->N_TX <= 0 || cfg->N_TX > 8)
        return 0;
    for (int tx_idx = 0; tx_idx < cfg->N_TX; tx_idx++) {
        if (phase_calib_tx_enabled(cfg, tx_idx))
            count++;
    }
    return count;
}

ErrStart_t table_gen(ne10_fft_cpx_float32_t *fft1d, CalibCfg_t *cfg, double *phase_error_table, int *IQ_idx_table)
{
    ErrStart_t errStat = {0};
    int num_bands = 0;
	int ridx_min;
    int ridx_min1;

    double start = 0.0;
    double step = 360.0 / 64.0;
    double sequence;

	if (fft1d == NULL || cfg == NULL || phase_error_table == NULL ||
	    IQ_idx_table == NULL || cfg->N_TX <= 0 || cfg->num_chirp[0] <= 0 ||
	    cfg->num_chirp[0] % cfg->N_TX != 0 || cfg->num_calib_tx <= 0 ||
	    cfg->num_calib_tx > cfg->N_TX ||
        phase_calib_active_tx_count(cfg) != cfg->num_calib_tx) {
		errStat.failed = 1;
		return errStat;
	}

	if(cfg->num_subframe == 1)
	{
		num_bands  = cfg->num_bands[0];
	}
	else if(cfg->num_subframe == 2)
	{
		if((cfg->num_bands[0] % cfg->num_bands[1] == 0) || (cfg->num_bands[1] % cfg->num_bands[0] == 0))
		{
            double Max_bands = cfg->num_bands[0];
            for (int j = 0; j < cfg->num_subframe; j ++)
            {
            	double val = cfg->num_bands[j];
                if (val > Max_bands)
                {
                    Max_bands = val;
                }
            }
			num_bands = Max_bands;
		}
		else
		{
            double Sum_bands = 0;
            for (int j = 0; j < cfg->num_subframe; j ++)
            {
            	double val = cfg->num_bands[j];
                Sum_bands += val;
            }
			num_bands = Sum_bands;
		}

	}
	if (num_bands <= 0 || num_bands > cfg->MAX_NUM_BANDS) {
		errStat.failed = 1;
		return errStat;
	}
    ridx_min = ceil(cfg->slope[0] * 2 * 2 * cfg->num_range_bins * cfg->filterNumRows / 3E8 / cfg->Fs);
    ridx_min1 = ridx_min - 1;
	if (ridx_min1 < 0)
		ridx_min1 = 0;
	else if (ridx_min1 >= cfg->num_range_bins) {
		errStat.failed = 1;
		return errStat;
	}
    LOG(LOG_DEBUG, "ridx_min = %d", ridx_min);

    ne10_fft_cpx_float32_t *rowSub4_fft1d = malloc(((cfg->num_range_bins - ridx_min1) * cfg->num_chirp[0]) * sizeof(ne10_fft_cpx_float32_t));
	if (rowSub4_fft1d == NULL) {
		errStat.failed = 1;
		return errStat;
	}

    for (int k = 0; k < cfg->num_chirp[0]; k++) {
        ne10_fft_cpx_float32_t* src_ptr = &fft1d[Get2DimArrayOff(ridx_min1, k, cfg->num_range_bins, cfg->num_chirp[0])];
        ne10_fft_cpx_float32_t* dst_ptr = &rowSub4_fft1d[Get2DimArrayOff(0, k, cfg->num_range_bins - ridx_min1, cfg->num_chirp[0])];
        memcpy(dst_ptr, src_ptr, (cfg->num_range_bins - ridx_min1) * sizeof(ne10_fft_cpx_float32_t));
    }

    // 找到每个chirp 1dFFT的最大值
    double *abs_fft1d = malloc((cfg->num_range_bins - ridx_min1) * cfg->num_chirp[0] * sizeof(double));
	if (abs_fft1d == NULL) {
		free(rowSub4_fft1d);
		errStat.failed = 1;
		return errStat;
	}
    for (int i = 0; i < (cfg->num_range_bins - ridx_min1) * cfg->num_chirp[0]; i ++)
    {
        abs_fft1d[i] = ComplexAbs(rowSub4_fft1d[i]);
    }
	free(rowSub4_fft1d);
    if (cfg->DEBUG) {
        double test[4][128];
        memcpy(test, abs_fft1d, sizeof(test));
        LOG(LOG_DEBUG, "");
    }
    int *maxIdx_test = malloc(cfg->num_chirp[0] * sizeof(int));
	if (maxIdx_test == NULL) {
		free(abs_fft1d);
		errStat.failed = 1;
		return errStat;
	}
    {
        int32_t numCol = cfg->num_chirp[0];
        int32_t numRow = cfg->num_range_bins - ridx_min1;

        for (int k = 0; k < numCol; k ++)
        {
            int32_t rowIdx = 0;
            double rowMax = abs_fft1d[k * numRow];
            for (int j = 1; j < numRow; j ++)
            {
                double val = abs_fft1d[k * numRow + j];
                if (val > rowMax)
                {
                    rowMax = val;
                    rowIdx = j;
                }
            }
            maxIdx_test[k] = rowIdx;
        }

        for (int i = 0; i < cfg->num_chirp[0]; i ++)
        {
            maxIdx_test[i] = maxIdx_test[i] + ridx_min1;
        }

        if (cfg->DEBUG) {
            int test[1024] = {0};
            memcpy(test, maxIdx_test, sizeof(test));
            LOG(LOG_DEBUG, "");
        }
    }

    free(abs_fft1d);

    /* N_TX is the physical scan stride.  The calibration waveform may scan
     * TX slots which have no antenna on this PCB, so only calib_tx_mask
     * columns may select the target range bin. */
    int maxIdx_test_sum = 0;
    int maxIdx_count = 0;
    for (int i = 0; i < cfg->num_chirp[0]; i ++)
    {
        if (phase_calib_tx_enabled(cfg, i % cfg->N_TX))
        {
            maxIdx_test_sum += maxIdx_test[i];
            maxIdx_count++;
        }
    }
    if (maxIdx_count == 0)
    {
        free(maxIdx_test);
        errStat.failed = 1;
        return errStat;
    }
    int maxIdx_test_avg =
        (int)round((double)maxIdx_test_sum / maxIdx_count);

    LOG(LOG_DEBUG, "maxIdx_test_avg = %d (calib_tx=%d)",
        maxIdx_test_avg, cfg->num_calib_tx);
    if (cfg->ddma_num[0] < 64)
    {
        printf("[PhaseCalib] chirp peak range bins:");
        for (int i = 0; i < cfg->num_chirp[0]; i++)
            printf(" %d", maxIdx_test[i]);
        printf("\n");
    }

    ne10_fft_cpx_float32_t *peak_all_chirps = malloc(cfg->num_chirp[0] * sizeof(ne10_fft_cpx_float32_t));
	if (peak_all_chirps == NULL) {
		free(maxIdx_test);
		errStat.failed = 1;
		return errStat;
	}
    memset(peak_all_chirps, 0, cfg->num_chirp[0] * sizeof(ne10_fft_cpx_float32_t));
    for (int i = 0; i < cfg->num_chirp[0]; i ++)
    {
        if (maxIdx_test[i] == 0)
        {
            LOG(LOG_DEBUG, "warning!, maxIdx == 0, which is dc...");
        }
        peak_all_chirps[i] = fft1d[i * cfg->num_range_bins + (int)maxIdx_test_avg];
    }
    free(maxIdx_test);
    if (cfg->DEBUG) {
        ne10_fft_cpx_float32_t test[1024] = {0};
        memcpy(test, peak_all_chirps, sizeof(test));
        LOG(LOG_DEBUG, "");
    }

    {
        ne10_fft_cpx_float32_t *peaks_2d = peak_all_chirps;
		int colsize = cfg->N_TX;
		int rowsize = cfg->num_chirp[0] / cfg->N_TX;


        for (int txIdx = 0; txIdx < cfg->N_TX; txIdx ++)
        {
            if (!phase_calib_tx_enabled(cfg, txIdx))
                continue;
            int num_phase = cfg->NUM_PHASE_PER_TX[txIdx];
			if (num_phase <= 0 || num_bands % num_phase != 0) {
				errStat.failed = 1;
				free(peak_all_chirps);
				return errStat;
			}

            // 单个TX的峰值
            ne10_fft_cpx_float32_t *peaks_tx = malloc(rowsize * sizeof(ne10_fft_cpx_float32_t));
			if (peaks_tx == NULL) {
				errStat.failed = 1;
				free(peak_all_chirps);
				return errStat;
			}
            for (int j = 0; j < rowsize; j ++)
            {
                peaks_tx[j] = peaks_2d[j * colsize + txIdx];
            }
            if (cfg->DEBUG) {
                ne10_fft_cpx_float32_t test[128] = {0};
                memcpy(test, peaks_tx, sizeof(test));
                LOG(LOG_DEBUG, "");
            }

            int trans_col, trans_row;
			if (cfg->ddma_num[0] == 64)
            {
                trans_col = 64;
            }
            else
            {
                trans_col = num_phase;
            }
            trans_row = rowsize / trans_col;
			if (trans_row <= 0 || rowsize % trans_col != 0) {
				errStat.failed = 1;
				free(peaks_tx);
				free(peak_all_chirps);
				return errStat;
			}

            ne10_fft_cpx_float32_t *peaks_tx_2d = malloc(trans_col * trans_row * sizeof(ne10_fft_cpx_float32_t));
			if (peaks_tx_2d == NULL) {
				errStat.failed = 1;
				free(peaks_tx);
				free(peak_all_chirps);
				return errStat;
			}

            transpose2Dim(peaks_tx, peaks_tx_2d, trans_row, trans_col);
            free(peaks_tx);
            if (cfg->DEBUG) {
                ne10_fft_cpx_float32_t test[64][2] = {0};
                memcpy(test, peaks_tx_2d, sizeof(test));
                LOG(LOG_DEBUG, "");
            }
            // 每个相位测了两个chirp，求每个峰值的平均值
            ne10_fft_cpx_float32_t *peaks_tx_avg = malloc(trans_col * sizeof(ne10_fft_cpx_float32_t));
			if (peaks_tx_avg == NULL) {
				errStat.failed = 1;
				free(peaks_tx_2d);
				free(peak_all_chirps);
				return errStat;
			}
            for (int j = 0; j < trans_col; j ++)
            {
                ne10_fft_cpx_float32_t avg = {0, 0};
                for (int i = 0; i < trans_row; i ++)
                {
                    avg.r += peaks_tx_2d[j * trans_row + i].r;
                    avg.i += peaks_tx_2d[j * trans_row + i].i;
                }
                peaks_tx_avg[j].r = avg.r / trans_row;
                peaks_tx_avg[j].i = avg.i / trans_row;
            }

            if (cfg->ddma_num[0] < 64 && trans_row > 1)
            {
                printf("[PhaseCalib] TX%d phase repeats (chirp:dB@deg):",
                       txIdx + 1);
                for (int j = 0; j < trans_col; j ++)
                {
                    printf(" P%d[", j);
                    for (int i = 0; i < trans_row; i ++)
                    {
                        ne10_fft_cpx_float32_t repeat =
                            peaks_tx_2d[j * trans_row + i];
                        int chirp_idx =
                            ((i * trans_col + j) * colsize) + txIdx;

                        printf("%sc%d:%.2f@%.1f", i == 0 ? "" : ",",
                               chirp_idx, Mag2Db(ComplexAbs(repeat)),
                               Anagle(repeat) * 180 / CALIB_PI);
                    }
                    printf("]");
                }
                printf("\n");
            }
            free(peaks_tx_2d);
            if (cfg->DEBUG) {
                ne10_fft_cpx_float32_t test[64] = {0};
                memcpy(test, peaks_tx_avg, sizeof(test));
                LOG(LOG_DEBUG, "");
            }
            // % 提取峰值相位
            double *phase_tx_avg = malloc(trans_col * sizeof(double));
			if (phase_tx_avg == NULL) {
				errStat.failed = 1;
				free(peaks_tx_avg);
				free(peak_all_chirps);
				return errStat;
			}
            for (int j = 0; j < trans_col; j ++)
            {
                phase_tx_avg[j] = Anagle(peaks_tx_avg[j]) * 180 / CALIB_PI;
            }

            if (cfg->DEBUG) {
                double test[64] = {0};
                memcpy(test, phase_tx_avg, sizeof(test));
                LOG(LOG_DEBUG, "");
            }
            //		    mag_tx_avg = abs(peaks_tx_avg);
            double *mag_tx_avg = malloc(trans_col * sizeof(double));
			if (mag_tx_avg == NULL) {
				errStat.failed = 1;
				free(phase_tx_avg);
				free(peaks_tx_avg);
				free(peak_all_chirps);
				return errStat;
			}
            for (int j = 0; j < trans_col; j ++)
            {
                mag_tx_avg[j] = ComplexAbs(peaks_tx_avg[j]);
            }
            free(peaks_tx_avg);

            if (cfg->DEBUG) {
                double test[64] = {0};
                memcpy(test, mag_tx_avg, sizeof(test));
                LOG(LOG_DEBUG, "");
            }

            if (cfg->ddma_num[0] == 64)
            {
                int minIdx = 0;
                int maxIdx = 0;
                for (int i = 1; i < trans_col; i++)
                {
                    if (mag_tx_avg[i] < mag_tx_avg[minIdx])
                        minIdx = i;
                    if (mag_tx_avg[i] > mag_tx_avg[maxIdx])
                        maxIdx = i;
                }
                printf("[PhaseCalib] TX%d 64phase magnitude: min=%.2f dB@%d, max=%.2f dB@%d, spread=%.2f dB\n",
                       txIdx + 1, Mag2Db(mag_tx_avg[minIdx]), minIdx,
                       Mag2Db(mag_tx_avg[maxIdx]), maxIdx,
                       Mag2Db(mag_tx_avg[maxIdx]) -
                           Mag2Db(mag_tx_avg[minIdx]));
            }

			if (cfg->ddma_num[0] < 64)
            {
                double maxMag = mag_tx_avg[0];
                double minMag = mag_tx_avg[0];
                for (int i = 1; i < trans_col; i ++)
                {
                    if (mag_tx_avg[i] > maxMag)
                    {
                        maxMag = mag_tx_avg[i];
                    }
                    if (mag_tx_avg[i] < minMag)
                    {
                        minMag = mag_tx_avg[i];
                    }
                }
                double maxMagDb = Mag2Db(maxMag);
                double minMagDb = Mag2Db(minMag);
                double magSpreadDb = maxMagDb - minMagDb;

                printf("[PhaseCalib] TX%d phase magnitude (dB):",
                       txIdx + 1);
                for (int i = 0; i < trans_col; i++)
                    printf(" %.2f", Mag2Db(mag_tx_avg[i]));
                printf("; spread=%.2f dB\n", magSpreadDb);

                if (magSpreadDb > cfg->max_mag_diff_db)
                {
                    LOG(LOG_DEBUG, "warning!, TX%d 峰值幅度变化范围 %.2f dB 超过 %.2f dB, 请先更改PA功率配置，移相器校准中断......",
                        txIdx + 1, magSpreadDb, cfg->max_mag_diff_db);
                    errStat.failed = 1;
                    errStat.failure_reason = CALIB_PROCESS_FAILURE_MAG_SPREAD;
                    errStat.failure_tx = txIdx + 1;
                    errStat.failure_value = magSpreadDb;
                    errStat.failure_limit = cfg->max_mag_diff_db;
					free(phase_tx_avg);
					free(mag_tx_avg);
					free(peak_all_chirps);
					return errStat;
                }
            }
            // free(mag_tx_avg);

            double *modphase_tx = malloc(trans_col * sizeof(double));
			if (modphase_tx == NULL) {
				errStat.failed = 1;
				free(phase_tx_avg);
				free(mag_tx_avg);
				free(peak_all_chirps);
				return errStat;
			}
            for (int i = 0; i < trans_col; i ++)
            {
                modphase_tx[i] = fmod((phase_tx_avg[i] - phase_tx_avg[0]), 360);
                if (modphase_tx[i] < 0)		// ??????????????
                {
                    modphase_tx[i] += 360;
                }
            }
            free(phase_tx_avg);

            if (cfg->DEBUG) {
                double test[64] = {0};
                memcpy(test, modphase_tx, sizeof(test));
                LOG(LOG_DEBUG, "");
            }

            if(trans_col == 64)
            {
                for (int i = 0; i < trans_col; i++) {
                    sequence = (start + (i * step));
                    cfg->modphase_tx[txIdx][i] = modphase_tx[i] - sequence;
                }

                for (int i = 0; i < trans_col; i++)
                {
                    cfg->mag_tx_avg[txIdx][i] = Mag2Db(mag_tx_avg[i]);
                }
            }
            else if(trans_col == num_phase)
            {
                for (int i = 0; i < trans_col; i++)
                {
                    cfg->phase_12[txIdx][i] = Mag2Db(mag_tx_avg[i]);
                }
            }

            free(mag_tx_avg);

            double *phase_tx_nominal = malloc(num_phase * sizeof(double));
			if (phase_tx_nominal == NULL) {
				errStat.failed = 1;
				free(modphase_tx);
				free(peak_all_chirps);
				return errStat;
			}
            for (int i = 0; i < num_phase; i ++)
            {
                phase_tx_nominal[i] = cfg->nominal_phase_table[txIdx][i];
            }

			if (cfg->ddma_num[0] == 64)
            {
                //		        % 将当前Tx用到的理想相位值和测量得到的所有（64个）相位求差
                double *phase_err_tx_table = malloc(trans_col * num_phase * sizeof(double));
				if (phase_err_tx_table == NULL) {
					errStat.failed = 1;
					free(modphase_tx);
					free(phase_tx_nominal);
					free(peak_all_chirps);
					return errStat;
				}
                for (int j = 0; j < num_phase; j ++)
                {
                    for (int i = 0; i < trans_col; i ++)
                    {
                        phase_err_tx_table[j * trans_col + i] = modphase_tx[i] - phase_tx_nominal[j];
                    }
                }
                if (cfg->DEBUG) {
                    double test[12][64] = {0};
                    memcpy(test, phase_err_tx_table, num_phase * 64 * sizeof(double));
                    LOG(LOG_DEBUG, "");
                }
                //  对每一列找最小值
                int *min_err_tx_idx = malloc(num_phase * sizeof(int));
				if (min_err_tx_idx == NULL) {
					errStat.failed = 1;
					free(phase_err_tx_table);
					free(modphase_tx);
					free(phase_tx_nominal);
					free(peak_all_chirps);
					return errStat;
				}
                for (int j = 0; j < num_phase; j ++)
                {
                    double minVal = fabs(phase_err_tx_table[j * trans_col]);
                    int minIdx = 0;
                    for (int i = 1; i < trans_col; i ++)
                    {
                        if (fabs(phase_err_tx_table[j * trans_col + i]) < minVal)
                        {
                            minVal = fabs(phase_err_tx_table[j * trans_col + i]);
                            minIdx = i;
                        }
                    }
                    min_err_tx_idx[j] = minIdx;
                }
                if (cfg->DEBUG) {
                    int test[12] = {0};
                    memcpy(test, min_err_tx_idx, num_phase * sizeof(int));
                    LOG(LOG_DEBUG, "");
                }
                //		        % 下面将误差添加到误差表
                double *phase_error_tx = malloc(num_phase * sizeof(double));
				if (phase_error_tx == NULL) {
					errStat.failed = 1;
					free(phase_err_tx_table);
					free(min_err_tx_idx);
					free(modphase_tx);
					free(phase_tx_nominal);
					free(peak_all_chirps);
					return errStat;
				}
                for (int colIdx = 0; colIdx < num_phase; colIdx ++)
                {
                    int idx = min_err_tx_idx[colIdx];
                    phase_error_tx[colIdx] = phase_err_tx_table[idx + trans_col * colIdx];
                }
                free(phase_err_tx_table);
                if (cfg->DEBUG) {
                    double test[12] = {0};
                    memcpy(test, phase_error_tx, sizeof(double) * num_phase);
                    LOG(LOG_DEBUG, "");
                }

                double *repmat_result1 = repmat1DimDouble(phase_error_tx, num_phase, num_bands/num_phase);
				if (repmat_result1 == NULL) {
					errStat.failed = 1;
					free(phase_error_tx);
					free(min_err_tx_idx);
					free(modphase_tx);
					free(phase_tx_nominal);
					free(peak_all_chirps);
					return errStat;
				}
                if (cfg->DEBUG) {
                    double test[12];
                    memcpy(test, repmat_result1, sizeof(test));
                    LOG(LOG_DEBUG, "");
                }
                free(phase_error_tx);

                for (int i = 0; i < num_bands; i ++)
                {
                    phase_error_table[txIdx + i * cfg->N_TX] = repmat_result1[i];
                }
                if (cfg->DEBUG) {
                    double test[16][8];
                    memcpy(test, phase_error_table, sizeof(test));
                    LOG(LOG_DEBUG, "");
                }

                free(repmat_result1);

                int *min_err_tx_idx_temp = malloc(num_phase * sizeof(int));
				if (min_err_tx_idx_temp == NULL) {
					errStat.failed = 1;
					free(min_err_tx_idx);
					free(modphase_tx);
					free(phase_tx_nominal);
					free(peak_all_chirps);
					return errStat;
				}
                for (int i = 0; i < num_phase; i ++)
                {
                    min_err_tx_idx_temp[i] = min_err_tx_idx[i] * 2;
                }
                free(min_err_tx_idx);
                int *repmat_result2 = repmat1DimInt(min_err_tx_idx_temp, num_phase, num_bands/num_phase);
				if (repmat_result2 == NULL) {
					errStat.failed = 1;
					free(min_err_tx_idx_temp);
					free(modphase_tx);
					free(phase_tx_nominal);
					free(peak_all_chirps);
					return errStat;
				}
                for (int i = 0; i < num_bands; i ++)
                {
                    IQ_idx_table[txIdx + i * cfg->N_TX] = repmat_result2[i];
                }
                free(min_err_tx_idx_temp);
                free(repmat_result2);
            }
            else
            {
                //		        % 计算误差
                //		        % 将误差添加到误差表
                double *phase_error_tx = malloc(trans_col * sizeof(double));
				if (phase_error_tx == NULL) {
					errStat.failed = 1;
					free(modphase_tx);
					free(phase_tx_nominal);
					free(peak_all_chirps);
					return errStat;
				}
                for (int i = 0; i < trans_col; i ++)
                {
                    phase_error_tx[i] = modphase_tx[i] - phase_tx_nominal[i];
                }
                double *repmat_result1 = repmat1DimDouble(phase_error_tx, trans_col, num_bands/num_phase);
				if (repmat_result1 == NULL) {
					errStat.failed = 1;
					free(phase_error_tx);
					free(modphase_tx);
					free(phase_tx_nominal);
					free(peak_all_chirps);
					return errStat;
				}
                for (int i = 0; i < num_bands; i ++)
                {
                    phase_error_table[txIdx + i * cfg->N_TX] = repmat_result1[i];
                }
                free(phase_error_tx);
                free(repmat_result1);
            }
            free(modphase_tx);
            free(phase_tx_nominal);
        }
        if (cfg->DEBUG) {
            double test0[16][8];
            int test1[16][8];
            memcpy(test0, phase_error_table, sizeof(test0));
            memcpy(test1, IQ_idx_table, sizeof(test1));
            LOG(LOG_DEBUG, "");
        }
        double sse = 0;
        double max_abs_err = 0;
        double sum_err = 0;
        int count = 0;
        for (int txIdx = 0; txIdx < cfg->N_TX; txIdx ++)
        {
            if (!phase_calib_tx_enabled(cfg, txIdx))
                continue;
            for (int colIdx = 0; colIdx < num_bands; colIdx ++)
            {
                double val = phase_error_table[colIdx * cfg->N_TX + txIdx];
                if (fabs(val) > 0)
                {
                    sse += val * val;
                    sum_err += val;
                    count ++;
                }
                if (fabs(val) > max_abs_err)
                {
                    max_abs_err = fabs(val);
                }
            }
        }
        if (count == 0)
        {
            errStat.failed = 1;
            free(peak_all_chirps);
            return errStat;
        }
        double mse = sse / count;
        errStat.rms = sqrt(mse);
        errStat.avg_err = sum_err / count;
        errStat.max_abs_err = max_abs_err;

        if (1)
        {
            LOG(LOG_INFO, "avg_err: %.4f,  max_abs_err: %.4f,  rms: %.4f", errStat.avg_err, errStat.max_abs_err, errStat.rms);
        }
        // if (cfg->DEBUG)
        if (0)
        {
            LOG(LOG_INFO, "");
            for (int txIdx = 0; txIdx < cfg->N_TX; txIdx ++)
            {
                if (!phase_calib_tx_enabled(cfg, txIdx))
                    continue;
                for (int colIdx = 0; colIdx < num_bands; colIdx ++)
                {
                    LOG(LOG_INFO, "%.4f ", phase_error_table[colIdx * cfg->N_TX + txIdx]);
                }
                LOG(LOG_INFO, "");
            }
            LOG(LOG_INFO, "校准前IQ LUT index： ");
            for (int txIdx = 0; txIdx < cfg->N_TX; txIdx ++)
            {
                if (!phase_calib_tx_enabled(cfg, txIdx))
                    continue;
                for (int colIdx = 0; colIdx < cfg->NUM_PHASE_PER_TX[txIdx]; colIdx ++)
                {
                    LOG(LOG_INFO, "%d ", IQ_idx_table[colIdx * cfg->N_TX + txIdx]);
                }
                LOG(LOG_INFO, "");
            }
        }
    }
    free(peak_all_chirps);

    return errStat;
}

static int ddma_config_gen(CalibCfg_t *cfg, int *IQ_idx_table)
{
    int num_bands = 0;

	if(cfg->num_subframe == 1)
	{
		num_bands  = cfg->num_bands[0];
	}
	else if(cfg->num_subframe == 2)
	{
		if((cfg->num_bands[0] % cfg->num_bands[1] == 0) || (cfg->num_bands[1] % cfg->num_bands[0] == 0))
		{
            double Max_bands = cfg->num_bands[0];
            for (int j = 0; j < cfg->num_subframe; j ++)
            {
            	double val = cfg->num_bands[j];
                if (val > Max_bands)
                {
                    Max_bands = val;
                }
            }
			num_bands = Max_bands;
		}
		else
		{
            double Sum_bands = 0;
            for (int j = 0; j < cfg->num_subframe; j ++)
            {
            	double val = cfg->num_bands[j];
                Sum_bands += val;
            }
			num_bands = Sum_bands;
		}

	}
	if (num_bands <= 0 || num_bands > cfg->MAX_NUM_BANDS ||
	    BYTE_ALIGN((num_bands * 6), 4) / 4 >
	        (int)(sizeof(cfg->DDMAProfileindexReg[0]) /
	              sizeof(cfg->DDMAProfileindexReg[0][0])))
		return -1;

    uint32_t *iq_hex_array = malloc(cfg->numMMIC * cfg->N_TX_PER_MMIC * cfg->MAX_NUM_BANDS * sizeof(uint16_t));
	if (iq_hex_array == NULL)
		return -1;

    uint8_t *pPfrRam = (uint8_t *)iq_hex_array;
    for (int chipIdx = 0; chipIdx < cfg->numMMIC; chipIdx ++)
    {
        for (int txIdx = 0; txIdx < cfg->N_TX_PER_MMIC; txIdx ++)
        {
            for (int phaseIdx = 0; phaseIdx < cfg->MAX_NUM_BANDS; phaseIdx ++)
            {
                int off = phaseIdx * cfg->N_TX_PER_MMIC * cfg->numMMIC + chipIdx * cfg->N_TX_PER_MMIC + txIdx;
                int LUT_idx = IQ_idx_table[off];
				if (LUT_idx < 0 || LUT_idx >= 128) {
					free(iq_hex_array);
					return -1;
				}

                *pPfrRam ++ = Phase_Shifter_IQ_LUT[0][LUT_idx];
                *pPfrRam ++ = Phase_Shifter_IQ_LUT[1][LUT_idx];
            }
        }

        if (chipIdx == 0)
        {
            LOG(LOG_DEBUG, "Master DDMA Profile RAM 0x00A0: ");
        }
        else
        {
            LOG(LOG_DEBUG, "Slave DDMA Profile RAM 0x00A0: ");
        }

        for (int i = 0; i < cfg->N_TX_PER_MMIC * cfg->MAX_NUM_BANDS / sizeof(uint16_t); i ++)
        {
            uint32_t val = iq_hex_array[i + chipIdx * cfg->N_TX_PER_MMIC * cfg->MAX_NUM_BANDS / sizeof(uint16_t)];
            LOG(LOG_DEBUG, "0x%08X\t", val);
            cfg->PhaseShifterReg[chipIdx][i] = val;
            if ((i % cfg->MAX_NUM_BANDS) == cfg->MAX_NUM_BANDS - 1)
            {
                LOG(LOG_INFO, "\t");
            }
        }
        LOG(LOG_INFO, "");
    }

	int *ddma_ram_table = malloc(cfg->N_TX_PER_MMIC * num_bands * sizeof(int));
	if (ddma_ram_table == NULL)
	{
		free(iq_hex_array);
		return -1;
	}

    for (int chipIdx = 0; chipIdx < cfg->numMMIC; chipIdx ++)
    {
        for (int txIdx = 0; txIdx < cfg->N_TX_PER_MMIC; txIdx ++)
        {
            int num_phases_tx = cfg->NUM_PHASE_PER_TX[txIdx + cfg->N_TX_PER_MMIC * chipIdx];
			if (fmod(num_bands, num_phases_tx) != 0)
            {
                LOG(LOG_ERROR, "warning! number of bands is NOT multiple of number of used phases for Tx ");
            }
			for (int i = 0; i < num_bands; i ++)
            {
                ddma_ram_table[txIdx + i * cfg->N_TX_PER_MMIC] = i + txIdx * cfg->MAX_NUM_BANDS;
            }
        }
        if (cfg->DEBUG)
        {
            int test[12][4];
            memcpy(test, ddma_ram_table, sizeof(test));
            LOG(LOG_DEBUG, "");
        }

		int ram_len = BYTE_ALIGN((num_bands * 6), 4);
        uint8_t *hex_array = malloc(ram_len * sizeof(uint8_t));
		if (hex_array == NULL)
		{
			free(ddma_ram_table);
			free(iq_hex_array);
			return -1;
		}
        memset(hex_array, 0, ram_len);
		for(int bandIdx = 0; bandIdx < num_bands; bandIdx ++)
        {
            uint32_t val = 0;
            for (int i = 0; i < cfg->N_TX_PER_MMIC; i ++)
            {
                val |= ddma_ram_table[bandIdx * cfg->N_TX_PER_MMIC + i] << (i * 6);
            }
            memcpy(hex_array + bandIdx * 6, &val, 3);
        }

        if (chipIdx == 0)
        {
            LOG(LOG_DEBUG, "Master DDMA Profile RAM 0x0304: ");
        }
        else
        {
            LOG(LOG_DEBUG, "Slave DDMA Profile RAM 0x0304: ");
        }

        uint32_t *pval = (uint32_t *)hex_array;
        for (int i = 0; i < ram_len / 4; i ++)
        {
            LOG(LOG_DEBUG, "0x%08X\t", pval[i]);
            cfg->DDMAProfileindexReg[chipIdx][i] = pval[i];
        }
        LOG(LOG_DEBUG, "");
		free(hex_array);
    }
	free(ddma_ram_table);
	free(iq_hex_array);
	/* Keep the X2100 ADC sample count selected by the stage configuration.
	 * The capture reference assigns 1024 here for its ZU3 waveform, but the
	 * verified X2100 calibration waveforms all deliver 506 valid samples. */
	cfg->num_chirp[0] = num_bands * 2 * cfg->N_TX;
	cfg->ddma_num[0] = num_bands;
	/* Stage 1/2 hold each DDMA code across one complete four-TX scan. */
	cfg->ddma_chirp[0] = cfg->N_TX;
	return 0;
}


static int ps_cal(CalibCfg_t *cfg, double *phase_error_table, int *IQ_idx_table)
{

    int num_bands = 0;

	if(cfg->num_subframe == 1)
	{
		num_bands  = cfg->num_bands[0];
	}
	else if(cfg->num_subframe == 2)
	{
		if((cfg->num_bands[0] % cfg->num_bands[1] == 0) || (cfg->num_bands[1] % cfg->num_bands[0] == 0))
		{
            double Max_bands = cfg->num_bands[0];
            for (int j = 0; j < cfg->num_subframe; j ++)
            {
            	double val = cfg->num_bands[j];
                if (val > Max_bands)
                {
                    Max_bands = val;
                }
            }
			num_bands = Max_bands;
		}
		else
		{
            double Sum_bands = 0;
            for (int j = 0; j < cfg->num_subframe; j ++)
            {
            	double val = cfg->num_bands[j];
                Sum_bands += val;
            }
			num_bands = Sum_bands;
		}

	}
    LOG(LOG_INFO, "phase error tolerance: %.3f", cfg->PHASE_ABS_TOL_DEG);
    for (int txIdx = 0; txIdx < cfg->N_TX; txIdx ++)
    {
		if (!phase_calib_tx_enabled(cfg, txIdx))
			continue;
		for (int colIdx = 0; colIdx < num_bands; colIdx ++)
        {
            if (phase_error_table[colIdx * cfg->N_TX + txIdx] > cfg->PHASE_ABS_TOL_DEG)
            {
                IQ_idx_table[colIdx * cfg->N_TX + txIdx] -= 1;	// % 降一档
            }
            else if (phase_error_table[colIdx * cfg->N_TX + txIdx] < -1 * cfg->PHASE_ABS_TOL_DEG)
            {
                IQ_idx_table[colIdx * cfg->N_TX + txIdx] += 1;	// % 升一档
            }
        }
    }

    if (cfg->ddma_num[0] == 64)
    {
        LOG(LOG_INFO, "[PhaseCalib] Selected IQ LUT indices:");
        for (int txIdx = 0; txIdx < cfg->N_TX; txIdx ++)
        {
            if (!phase_calib_tx_enabled(cfg, txIdx))
                continue;
            printf("[PhaseCalib] TX%d:", txIdx + 1);
            for (int colIdx = 0; colIdx < cfg->NUM_PHASE_PER_TX[txIdx]; colIdx ++)
            {
                printf(" %d", IQ_idx_table[colIdx * cfg->N_TX + txIdx]);
            }
            printf("\n");
        }
    }

    return ddma_config_gen(cfg, IQ_idx_table);
}

static double *CalMaxVal_2Dim(CalibCfg_t *cfg, double *inBuf, int32_t numRow, int32_t numCol, int32_t *maxRowIdx, double *maxVal, int sfIdx)
{
	int ridx_min;
    double rowMax = 0;
    double colMax = 0;
    double doppCutMax = 0;
    double val;

    double *max_rbin = malloc(numRow * sizeof(double));
    double *doppCut = malloc(numCol * sizeof(double));
	if (max_rbin == NULL || doppCut == NULL)
	{
		free(max_rbin);
		free(doppCut);
		return NULL;
	}

    int32_t rowIdx = 0;
    for (int k = 0; k < numRow; k ++)
    {
        for (int j = 0; j < numCol; j ++)
        {
            val = inBuf[j * numRow + k];
            if (val > rowMax)
            {
                rowMax = val;
            }
        }
        max_rbin[k] = rowMax;
        rowMax = 0;
    }

    ridx_min = ceil(cfg->slope[sfIdx] * 2 * 2 * cfg->num_range_bins * cfg->filterNumRows / 3E8 / cfg->Fs);
    ridx_min = ridx_min - 2;
	if (ridx_min < 0)
		ridx_min = 0;
	else if (ridx_min >= numRow)
		ridx_min = numRow - 1;

    for (int k = ridx_min; k < numRow; k ++)
    {
        val = max_rbin[k];
        if (val > colMax)
        {
            colMax = val;
            rowIdx = k;
        }
    }

    for (int j = 0; j < numCol; j ++)
    {
        doppCut[j] = inBuf[j * numRow + rowIdx];
        if (doppCut[j] > doppCutMax)
        {
            doppCutMax = doppCut[j];
            *maxVal = doppCutMax;
            *maxRowIdx = j;
        }
    }
    LOG(LOG_DEBUG, "rowIdx = %d", rowIdx);
    LOG(LOG_DEBUG, "ridx_min = %d", ridx_min);
    LOG(LOG_DEBUG, "maxRowIdx = %d", *maxRowIdx);
	free(max_rbin);
    return doppCut;
}

// %% 2.5.3) 找出每个子带的峰值, 计算空子带
double EEmptyBandRRelativePPower(double *inBuf, CalibCfg_t *cfg, int32_t maxRowIdx, int sfIdx)
{
	int32_t subband_bin_num;
	if (inBuf == NULL || cfg == NULL || cfg->num_bands[sfIdx] <= 0 ||
	    cfg->num_signal_bands <= 0 ||
	    cfg->num_signal_bands >= cfg->num_bands[sfIdx])
		return NAN;


	int nfft;
	if (cfg->num_chirp[sfIdx] % 3 == 0)
	{
		nfft = CalFftNum(cfg->num_chirp[sfIdx] / 3) * 3;
	}
	else
	{
		nfft = CalFftNum(cfg->num_chirp[sfIdx]);
	}

	double *subband_peaks = malloc(cfg->num_bands[sfIdx] * sizeof(double));
	if (subband_peaks == NULL)
		return NAN;

	subband_bin_num = nfft / cfg->num_bands[sfIdx];


	/* The bands are periodic in Doppler.  Starting at the strongest band,
	 * sample exactly one peak from each band and wrap at the FFT boundary. */
	for (int band = 0; band < cfg->num_bands[sfIdx]; band ++)
	{
		int32_t peak_idx = (maxRowIdx + band * subband_bin_num) % nfft;
		subband_peaks[band] = inBuf[peak_idx];
	}


	for (int i = 0; i < cfg->num_bands[sfIdx]; i++)
	{
		subband_peaks[i] = Mag2Db(subband_peaks[i]);
	}

	for (int i = 0; i < cfg->num_bands[sfIdx] - 1; i++) {
		for (int j = 0; j < cfg->num_bands[sfIdx] - i - 1; j++) {
			if (subband_peaks[j] < subband_peaks[j + 1]) {
				double temp = subband_peaks[j];
				subband_peaks[j] = subband_peaks[j + 1];
				subband_peaks[j + 1] = temp;
			}
		}
	}

    for (int i = 0; i < cfg->num_bands[sfIdx]; i++)
    {
        LOG(LOG_DEBUG, "maxRowIdx[%d] = %f", i, subband_peaks[i]);
    }

	double empty_band_power_dbc = subband_peaks[cfg->num_signal_bands] -
				      subband_peaks[cfg->num_signal_bands - 1];

	free(subband_peaks);

    return empty_band_power_dbc;
}

ErrStart_t firstCalibrationProcess(CalibCfg_t *cfg, int16_t *adc_data, int *IQ_idx_table)
{
    ErrStart_t errStat = {0};

    int16_t *adc64;
    //1dFFT
    adc64 = (int16_t *)malloc(cfg->num_chirp[0] * cfg->num_adc[0] * sizeof(int16_t));
	if (adc64 == NULL) {
		errStat.failed = 1;
		return errStat;
	}

    for (int k = 0; k < cfg->num_chirp[0]; k ++)
    {
        for (int j = 0; j < cfg->num_adc[0]; j ++)
        {
            int32_t offsrc, offdst;
            offsrc = Get3DimArrayOff(0, j, k, cfg->N_RX, cfg->num_adc[0], cfg->num_chirp[0]);
            offdst = Get2DimArrayOff(j, k, cfg->num_adc[0], cfg->num_chirp[0]);
            adc64[offdst] = adc_data[offsrc];
        }
    }

    if (cfg->DEBUG) {
        int16_t test[4][256];
        memcpy(test, adc64, sizeof(test));
        LOG(LOG_DEBUG, "");
    }
    //1dFFT
    ne10_fft_cpx_float32_t *fft1d = fft1d_func(adc64, cfg, 1, 0);
    if (fft1d == NULL) {
        free(adc64);
        errStat.failed = 1;
        return errStat;
    }

    //计算相位误差，生成误差表和IQ index表
    double *phase_error_table = malloc(cfg->N_TX * cfg->MAX_NUM_BANDS * sizeof(double));
	if (phase_error_table == NULL) {
		free(fft1d);
		free(adc64);
		errStat.failed = 1;
		return errStat;
	}
    memset(phase_error_table, 0, cfg->N_TX * cfg->MAX_NUM_BANDS * sizeof(double));

    //相位校准的tolerance
    cfg->PHASE_ABS_TOL_DEG = 1.6;

    errStat = table_gen(fft1d, cfg, phase_error_table, IQ_idx_table);
	if (errStat.failed)
	{
		free(fft1d);
		free(phase_error_table);
		free(adc64);
		return errStat;
	}

    //生成的表格基础上进行校准， 并生成新的移相器RAM配置
    if (ps_cal(cfg, phase_error_table, IQ_idx_table) != 0)
		errStat.failed = 1;

    free(fft1d);
    free(phase_error_table);
    free(adc64);

    return errStat;
}

ErrStart_t secondCalibrationProcess(CalibCfg_t *cfg, int16_t *adc_data, int *IQ_idx_table)
{

    int16_t *adc12;
    ne10_fft_cpx_float32_t *fft1d;
    ErrStart_t errStat = {0};

    adc12 = (int16_t *)malloc(cfg->num_chirp[0] * cfg->num_adc[0] * sizeof(int16_t));
	if (adc12 == NULL) {
		errStat.failed = 1;
		return errStat;
	}

    for (int k = 0; k < cfg->num_chirp[0]; k ++)
    {
        for (int j = 0; j < cfg->num_adc[0]; j ++)
        {
            int32_t offsrc, offdst;
            offsrc = Get3DimArrayOff(0, j, k, cfg->N_RX, cfg->num_adc[0], cfg->num_chirp[0]);
            offdst = Get2DimArrayOff(j, k, cfg->num_adc[0], cfg->num_chirp[0]);
            adc12[offdst] = adc_data[offsrc];
        }
    }

    fft1d = fft1d_func(adc12, cfg, 1, 0);
    if (fft1d == NULL) {
        free(adc12);
        errStat.failed = 1;
        return errStat;
    }

    //计算相位误差，生成误差表和IQ index表
    double *phase_error_table = malloc(cfg->N_TX * cfg->MAX_NUM_BANDS * sizeof(double));
	if (phase_error_table == NULL) {
		free(fft1d);
		free(adc12);
		errStat.failed = 1;
		return errStat;
	}
    memset(phase_error_table, 0, cfg->N_TX * cfg->MAX_NUM_BANDS * sizeof(double));
    cfg->PHASE_ABS_TOL_DEG = 1.6;
    errStat = table_gen(fft1d, cfg, phase_error_table, IQ_idx_table);
    if (errStat.failed)
    {
        LOG(LOG_ERROR, "warning! 校准过程出错，中止 ");
        free(phase_error_table);
        free(fft1d);
        free(adc12);
        return errStat;
    }
    //生成的表格基础上进行校准， 并生成新的移相器RAM配置
    if (ps_cal(cfg, phase_error_table, IQ_idx_table) != 0)
		errStat.failed = 1;

    free(fft1d);
    free(phase_error_table);
    free(adc12);

    return errStat;
}

void printCalibCfg(CalibCfg_t *cfg, int nfft)
{
    printf("======= cfg->num_range_bins: %d nfft:%d Rx:%d\n", cfg->num_range_bins, nfft, cfg->N_RX);
    printf("CalibCfg_t contents:\n");
    printf("  cascade: %d\n", cfg->cascade);
    printf("  numMMIC: %d\n", cfg->numMMIC);
    printf("  modulation: %d\n", cfg->modulation);
    printf("  num_subframe: %d\n", cfg->num_subframe);
    for (int sf = 0; sf < cfg->num_subframe && sf < 4; sf++) {
        printf("  Subframe %d:\n", sf);
        printf("    num_adc: %d\n", cfg->num_adc[sf]);
        printf("    num_chirp: %d\n", cfg->num_chirp[sf]);
        printf("    ddma_num: %d\n", cfg->ddma_num[sf]);
        printf("    ddma_chirp: %d\n", cfg->ddma_chirp[sf]);
        printf("    num_bands: %d\n", cfg->num_bands[sf]);
        printf("    slope: %f\n", cfg->slope[sf]);
        printf("    empty_band_power_dbc: %f\n", cfg->empty_band_power_dbc[sf]);
    }
    printf("  num_range_bins: %d\n", cfg->num_range_bins);
    printf("  MAX_NUM_BANDS: %d\n", cfg->MAX_NUM_BANDS);
    printf("  NUM_PHASE_PER_TX: %d %d %d %d %d %d %d %d\n",
           cfg->NUM_PHASE_PER_TX[0], cfg->NUM_PHASE_PER_TX[1],
           cfg->NUM_PHASE_PER_TX[2], cfg->NUM_PHASE_PER_TX[3],
           cfg->NUM_PHASE_PER_TX[4], cfg->NUM_PHASE_PER_TX[5],
           cfg->NUM_PHASE_PER_TX[6], cfg->NUM_PHASE_PER_TX[7]);
    printf("  N_TX: %d\n", cfg->N_TX);
    printf("  calib_tx_mask: 0x%02x\n", (unsigned int)cfg->calib_tx_mask);
    printf("  num_calib_tx: %d\n", cfg->num_calib_tx);
    printf("  N_RX: %d\n", cfg->N_RX);
    printf("  N_TX_PER_MMIC: %d\n", cfg->N_TX_PER_MMIC);
    printf("  num_signal_bands: %d\n", cfg->num_signal_bands);
    printf("  MAX_ITERATION: %d\n", cfg->MAX_ITERATION);
    printf("  good_empty_dbc: %f\n", cfg->good_empty_dbc);
    printf("  DEBUG: %d\n", cfg->DEBUG);
    printf("  filterNumRows: %d\n", cfg->filterNumRows);
    printf("  Fs: %f\n", cfg->Fs);
    printf("  max_mag_diff_db: %f\n", cfg->max_mag_diff_db);
    printf("  max_empty_dbc: %f\n", cfg->max_empty_dbc);
    printf("  PHASE_ABS_TOL_DEG: %f\n", cfg->PHASE_ABS_TOL_DEG);
}

ErrStart_t checkConvergenceProcess(CalibCfg_t *cfg, int16_t *adc_data, int *IQ_idx_table, int sfIdx)
{
    ErrStart_t errStat = {0};
    int16_t *adc_ddm;
    int32_t maxRowIdx = 0;
    double maxVal = 0;
    double *fft2dSumRx;
    double *doppCut;
    double empty_band_power_dbc;
    ne10_fft_cpx_float32_t *fft1d;
    ne10_fft_cpx_float32_t *fft2d;

    //	    %% 2.5.2） 做1dFFT, 2dFFT, 非相干累积
    adc_ddm = (int16_t *)malloc(cfg->num_chirp[sfIdx] * cfg->num_adc[sfIdx] * cfg->N_RX * sizeof(int16_t));
	if (adc_ddm == NULL) {
		errStat.failed = 1;
		return errStat;
	}

    for (int k = 0; k < cfg->num_chirp[sfIdx]; k ++)
    {
        for (int j = 0; j < cfg->num_adc[sfIdx]; j ++)
        {
            for (int i = 0; i < cfg->N_RX; i ++)
            {
                int32_t offsrc, offdst;
                offsrc = Get3DimArrayOff(i, j, k, cfg->N_RX, cfg->num_adc[sfIdx], cfg->num_chirp[sfIdx]);
                offdst = Get3DimArrayOff(j, k, i, cfg->num_adc[sfIdx], cfg->num_chirp[sfIdx], cfg->N_RX);

                adc_ddm[offdst] = adc_data[offsrc];
            }
        }
    }

    fft1d = fft1d_func(adc_ddm, cfg, cfg->N_RX, sfIdx);
    if (fft1d == NULL) {
        free(adc_ddm);
        errStat.failed = 1;
        return errStat;
    }
    free(adc_ddm);
    fft2d = fft2d_func(fft1d, cfg, cfg->N_RX,sfIdx);
    if (fft2d == NULL) {
        free(fft1d);
        errStat.failed = 1;
        return errStat;
    }
    free(fft1d);


    int nfft;
    if (cfg->num_chirp[sfIdx] % 3 == 0)
    {
        nfft = CalFftNum(cfg->num_chirp[sfIdx] / 3) * 3;
    }
    else
    {
        nfft = CalFftNum(cfg->num_chirp[sfIdx]);
    }

    {
        printCalibCfg(cfg, nfft);
    }

  // 返回最高峰值所在rbin的doppler cut
    fft2dSumRx = CalSumOfFft2d(fft2d,cfg->num_range_bins, nfft, cfg->N_RX);
    if (fft2dSumRx == NULL) {
        free(fft2d);
        errStat.failed = 1;
        return errStat;
    }
    free(fft2d);

    doppCut = CalMaxVal_2Dim(cfg, fft2dSumRx, cfg->num_range_bins, nfft, &maxRowIdx, &maxVal, sfIdx);
    if (doppCut == NULL) {
        free(fft2dSumRx);
        errStat.failed = 1;
        return errStat;
    }
    empty_band_power_dbc = EEmptyBandRRelativePPower(doppCut, cfg, maxRowIdx, sfIdx);
	if (isnan(empty_band_power_dbc)) {
		free(doppCut);
		free(fft2dSumRx);
		errStat.failed = 1;
		return errStat;
	}

    free(doppCut);
    free(fft2dSumRx);
    cfg->empty_band_power_dbc[sfIdx] = empty_band_power_dbc;
    if (empty_band_power_dbc <= cfg->good_empty_dbc)
    {
        LOG(LOG_INFO, "-->空子带能量%.2fdBc, 达到较好标准，可提前结束迭代.", empty_band_power_dbc);
    }
    else if (empty_band_power_dbc <= cfg->max_empty_dbc)
    {
        LOG(LOG_INFO, "-->空子带能量%.2fdBc, 达到最低标准，记录为候选结果.", empty_band_power_dbc);
    }
    else
    {
        LOG(LOG_INFO, "-->空子带能量%.2fdBc, 继续迭代.", empty_band_power_dbc);
    }



    return errStat;
}
