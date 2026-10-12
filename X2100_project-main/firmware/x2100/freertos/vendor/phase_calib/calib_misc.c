#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "NE10_dsp.h"
#include "NE10_types.h"
#include "heap_malloc.h"
#include "window.h"

#include "calib_misc.h"

int CalFftNum(int size)
{
	int nfft;

	nfft = pow(2, ceil(log2(size)));

	return nfft;
}

double Anagle(ne10_fft_cpx_float32_t c)
{
    return atan2((double)c.i, (double)c.r);
}

double AtanDeg(double val)
{
    return atan(val) * 180 / CALIB_PI;
}

double Norm2(double *v, int n)
{
    double t = 0;

    for (int i = 0; i < n; i ++)
    {
        t += v[i] * v[i];
    }

    return sqrt(t);
}
// Inverse sine in degrees
double AsinDeg(double val)
{
//    double degVal = 180.0 / PI;
//
//    return asin(val) * degVal;

    double radian;

    if (val <= -1)
    {
        radian = -CALIB_PI / 2;
    }
    else if (val >= 1)
    {
        radian = CALIB_PI / 2;
    }
    else
    {
        radian = atan(val / sqrt(1 - val * val));
    }

    return radian * 180 / CALIB_PI;
}

double CosDeg(double val)
{
    double degVal = 180.0 / CALIB_PI;
    return cos(val / degVal);
}

double SinDeg(double val)
{
    double degVal = 180.0 / CALIB_PI;
    return sin(val / degVal);
}

double Mag2Db(double mag)
{
    return (double)20 * log10(mag);
}

double ComplexAbs(ne10_fft_cpx_float32_t c)
{
    return sqrt(c.r * c.r + c.i * c.i);
}

int ComplexToDb(ne10_fft_cpx_float32_t *inBuf, int32_t size, double *outDb)
{
    double *pDb;

    pDb = outDb;

    for (int i = 0; i < size; i ++)
    {
        pDb[i] = Mag2Db(ComplexAbs(inBuf[i]));
    }

    return 0;
}

int32_t Get4DimArrayOff(int x, int y, int z, int l, int xsize, int ysize, int zsize, __attribute__((unused))int lsize)
{
    int32_t off = 0;

    off = l * xsize * ysize * zsize + z * xsize * ysize + y * xsize + x;

    return off;
}

int32_t Get3DimArrayOff(int x, int y, int z, int xsize, int ysize, __attribute__((unused))int zsize)
{
    int32_t off = 0;

    off = z * xsize * ysize + y * xsize + x;

    return off;
}

int32_t Get2DimArrayOff(int x, int y, int xsize, __attribute__((unused))int ysize)
{
    return y * xsize + x;
}

void UnWrap(double *wrappedPhase, double *unwrappedPhase, int n)
{
    unwrappedPhase[0] = wrappedPhase[0];
    double diff;

    for (int i = 1; i < n; i++)
    {
        diff = wrappedPhase[i] - unwrappedPhase[i - 1];
        if (diff > CALIB_PI)
        {      // positive jump
            unwrappedPhase[i] = wrappedPhase[i] - 2 * CALIB_PI * round(diff / (2 * CALIB_PI));
        }
        else if (diff < -CALIB_PI)
        {   // negative jump
            unwrappedPhase[i] = wrappedPhase[i] + 2 * CALIB_PI * round(diff / (2 * CALIB_PI));
        }
        else
        {                     // no jump
            unwrappedPhase[i] = wrappedPhase[i];
        }
    }
}

void Reshape2DimArray(int32_t rowSize, int32_t colSize, void *pSrcBuf, void *pDstBuf, int32_t sizeOfType)
{
    for (int j = 0; j < colSize; j ++)
    {
        for (int i = 0; i < rowSize; i ++)
        {
            memcpy((void *)((char *)(pDstBuf) + (i * colSize + j) * sizeOfType), (void *)((char *)(pSrcBuf) + (j * rowSize + i) * sizeOfType), sizeOfType);
//            pDstBuf[i * colSize + j] = pSrcBuf[j * rowSize + i];
        }
    }
}

static int interp1(const double x[], const double y[], int length, double x2, double y2[])
{

    /* Check if x2 is within range */
    if (!((x[0] <= x2 && x2 < x[length - 1]) || (x[0] > x2 && x2 >= x[length - 1])))
        return -1;

    /*
     Use a binary / half-interval search algorithm
     to find the relevant index in x[]. For details:
     http://en.wikipedia.org/wiki/Binary_search_algorithm
     */
    int imin = 0, imax = length - 1, imid;
    int sgn = (x[imin] < x[imax]) ? 1 : -1;
    while (imax - imin > 1)
    {

        /* calculate midpoint */
        imid = (imin + imax) / 2;

        /* check which half to continue with */
        if ((x2 - x[imid]) * sgn >= 0)
            imin = imid;
        else if ((x2 - x[imid]) * sgn < 0)
            imax = imid;
    }

    (*y2) = (y[imax] - y[imin]) / (x[imax] - x[imin]) * (x2 - x[imin]) + y[imin];

    return imin;
}

int Interp2Linear(double *pXOrig, int32_t xOrigLen, double *pYOrig, int32_t yOrigLen, double *pOriginBuf,
    double *pXScaler, int32_t xScalerLen, double *pYScaler, int32_t yScalerLen, double *pScalerBuf)
{
    int ret;
    double val;

    double *pXScalerOut;
    double *pYScalerIn;
    double *pYScalerOut;

    pXScalerOut = malloc(xScalerLen * yOrigLen * sizeof(double));
    pYScalerIn = malloc(xScalerLen * yOrigLen * sizeof(double));
    pYScalerOut = malloc(xScalerLen * yScalerLen * sizeof(double));

    for (int i = 0; i < yOrigLen; i ++)
    {
        for (int j = 0; j < xScalerLen; j ++)
        {
            ret = interp1(pXOrig, pOriginBuf + i * xOrigLen, xOrigLen, pXScaler[j], &val);
            if (ret < 0)
            {
                break;
            }
            pXScalerOut[i * xScalerLen + j] = val;
        }
    }

    Reshape2DimArray(xScalerLen, yOrigLen, pXScalerOut, pYScalerIn, sizeof(double));


    for (int i = 0; i < xScalerLen; i ++)
    {
        for (int j = 0; j < yScalerLen; j ++)
        {
            ret = interp1(pYOrig, pYScalerIn + i * yOrigLen, yOrigLen, pYScaler[j], &val);
            if (ret < 0)
            {
                break;
            }
            pYScalerOut[i * yScalerLen + j] = val;
        }
    }

    Reshape2DimArray(yScalerLen, xScalerLen, pYScalerOut, pScalerBuf, sizeof(double));

    free(pXScalerOut);
    free(pYScalerIn);
    free(pYScalerOut);

    return 0;
}

int Interp1Linear(double *pXOrig, int32_t xOrigLen, double *pOrigBuf, double *pXScaler, int32_t xScalerLen, double *pScalerBuf)
{
    double val;
    int ret = 0;

    for (int i = 0; i < xScalerLen; i ++)
    {
        ret = interp1(pXOrig, pOrigBuf, xOrigLen, pXScaler[i], &val);
        if (ret < 0)
        {
            break;
        }
        pScalerBuf[i] = val;
    }

    return ret;
}

int FftShift2Dim(const void *inFftBuf, int32_t rowNum, int32_t colNum, int32_t sizeOfType, void *outShiftBuf)
{
    void *pInFftRowBuf;
    void *pShiftRowBuf;
    void *pXShiftBuf;
    void *pYShiftBuf;
    int32_t half;

    pXShiftBuf = malloc(rowNum * colNum * sizeOfType);
    if (pXShiftBuf == NULL)
    {
        return -1;
    }

    pYShiftBuf = outShiftBuf;

    // first flip the x axis
    half = rowNum / 2;
    for (int j = 0; j < colNum; j ++)
    {
        pInFftRowBuf = (void *)((uint8_t *)inFftBuf + j * rowNum * sizeOfType);
        pShiftRowBuf = (void *)((uint8_t *)pXShiftBuf + j * rowNum * sizeOfType);

        for (int i = 0; i < half; i ++)
        {
//            pShiftRowBuf[j][i] = pInFftRowBuf[j][half + i];
            memcpy((void *)((uint8_t *)pShiftRowBuf + i * sizeOfType), (void *)((uint8_t *)pInFftRowBuf + (half + i) * sizeOfType), sizeOfType);
        }

        for (int i = 0; i < half; i ++)
        {
//            pShiftRowBuf[j][half + i] = pInFftRowBuf[j][i];
            memcpy((void *)((uint8_t *)pShiftRowBuf + (half + i) * sizeOfType), (void *)((uint8_t *)pInFftRowBuf + i * sizeOfType), sizeOfType);
        }
    }

    // first flip the y axis
    half = colNum / 2;
    for (int j = 0; j < rowNum; j ++)
    {
        for (int i = 0; i < half; i ++)
        {
            pInFftRowBuf = (void *)((uint8_t *)pXShiftBuf + ((i + half) * rowNum + j) * sizeOfType);
            pShiftRowBuf = (void *)((uint8_t *)pYShiftBuf + (i * rowNum + j) * sizeOfType);
//            pShiftRowBuf[half + i][j] = pInFftRowBuf[i][j];
            memcpy((void *)pShiftRowBuf, (void *)pInFftRowBuf, sizeOfType);
        }

        for (int i = 0; i < half; i ++)
        {
            pInFftRowBuf = (void *)((uint8_t *)pXShiftBuf + (i * rowNum + j) * sizeOfType);
            pShiftRowBuf = (void *)((uint8_t *)pYShiftBuf + ((i + half) * rowNum + j) * sizeOfType);
//            pShiftRowBuf[i][j] = pInFftRowBuf[i + half][j];
            memcpy((void *)pShiftRowBuf, (void *)pInFftRowBuf, sizeOfType);
        }
    }

    free(pXShiftBuf);

    return 0;
}

int FftShift1Dim(const void *inFftBuf, int32_t rowNum, int32_t colNum, int32_t sizeOfType, void *outShiftBuf)
{
    void *pShiftBuf;
    void *pInFftRowBuf;
    void *pShiftRowBuf;
    int32_t half;

    pShiftBuf = outShiftBuf;

    half = rowNum / 2;

    for (int j = 0; j < colNum; j ++)
    {
        pInFftRowBuf = (void *)((uint8_t *)inFftBuf + j * rowNum * sizeOfType);
        pShiftRowBuf = (void *)((uint8_t *)pShiftBuf + j * rowNum * sizeOfType);
        for (int i = 0; i < half; i ++)
        {
            memcpy((void *)((uint8_t *)pShiftRowBuf + i * sizeOfType), (void *)((uint8_t *)pInFftRowBuf + (half + i) * sizeOfType),  sizeOfType);
//            pShiftRowBuf[i] = pInFftRowBuf[half + i];
        }

        for (int i = 0; i < half; i ++)
        {
            memcpy((void *)((uint8_t *)pShiftRowBuf + (half + i) * sizeOfType), (void *)((uint8_t *)pInFftRowBuf + i * sizeOfType),  sizeOfType);
//            pShiftRowBuf[half + i] = pInFftRowBuf[i];
        }
    }

    return 0;
}

void LinspaceDouble(double x1, double x2, int32_t n, double *inLinSpaceBuf)
{
    double interval;

    interval = (x2 - x1) / (n - 1);

    for (int i = 0; i < n; i ++)
    {
        inLinSpaceBuf[i] = x1 + i * interval;
    }
}

void CalMaxVal_1Dim(double *inBuf, int32_t num, int32_t *maxIdx, double *maxVal)
{
    int32_t idx = 0;
    double val = 0;

    for (int i = 0; i < num; i ++)
    {
        if (inBuf[i] > val)
        {
            idx = i;
            val = inBuf[i];
        }
    }
    *maxVal = val;
    *maxIdx = idx;
}


ne10_fft_cpx_float32_t *Fft1D_DiscardHalf(ne10_fft_cpx_float32_t *inBuf, int xsize, int ysize, int zsize)
{
	int x, y, z;
	int srcoff, dstoff;
	ne10_fft_cpx_float32_t *outBuf = malloc(xsize / 2 * ysize * zsize * sizeof(ne10_fft_cpx_float32_t));
	if (outBuf == NULL)
		return NULL;

	for (z = 0; z < zsize; z ++)
	{
		for (y = 0; y < ysize; y ++)
		{
			for (x = 0; x < xsize / 2; x ++)
			{
				srcoff = Get3DimArrayOff(x, y, z, xsize, ysize, zsize);
				dstoff = Get3DimArrayOff(x, y, z, xsize / 2, ysize, zsize);
				outBuf[dstoff] = inBuf[srcoff];
			}
		}
	}

	return outBuf;
}

ne10_fft_cpx_float32_t *RangeAddWin_CalFft(int16_t *inBuf, int xsize, int ysize, int zsize, int nfft)
{
	int x, y, z;
	int srcoff, dstoff;
	ne10_fft_r2c_cfg_float32_t cfg;
	ne10_fft_cpx_float32_t *in, *out;
	ne10_fft_cpx_float32_t *pin;
	ne10_fft_cpx_float32_t *pout;

	ne10_fft_cpx_float32_t *outBuf = malloc(nfft * ysize * zsize * sizeof(ne10_fft_cpx_float32_t));
    double *winBuf = malloc((xsize + 1) * sizeof(double));

	cfg = ne10_fft_alloc_r2c_float32(nfft);
	in = (ne10_fft_cpx_float32_t*) malloc(sizeof(ne10_fft_cpx_float32_t) * nfft);
	out = (ne10_fft_cpx_float32_t*) malloc(sizeof(ne10_fft_cpx_float32_t) * nfft);
	if (outBuf == NULL || winBuf == NULL || cfg == NULL || in == NULL || out == NULL)
	{
		if (cfg != NULL)
			ne10_fft_destroy_r2c_float32(cfg);
		free(outBuf);
		free(winBuf);
		free(in);
		free(out);
		return NULL;
	}

    hann(winBuf, xsize + 1, 0);

	for (z = 0; z < zsize; z ++)
	{
		for (y = 0; y < ysize; y ++)
		{
			pin = (ne10_fft_cpx_float32_t *)in;
			pout = (ne10_fft_cpx_float32_t *)out;

			for (x = 0; x < xsize; x ++)
			{
				srcoff = Get3DimArrayOff(x, y, z, xsize, ysize, zsize);
				pin[x].r = (float)(winBuf[x + 1] * inBuf[srcoff]);
                pin[x].i = 0.0f;
			}
			for (; x < nfft; x ++)
			{
				pin[x].r = 0.0f;
                pin[x].i = 0.0f;
			}
			ne10_fft_c2c_1d_float32_mxu_ai(out, in, cfg); // 执行变换
			for (x = 0; x < nfft; x ++)
			{
				dstoff = Get3DimArrayOff(x, y, z, nfft, ysize, zsize);
				outBuf[dstoff] = pout[x];
			}
		}
	}

	ne10_fft_destroy_r2c_float32(cfg);
	free(in);
	free(out);

    free(winBuf);
	return outBuf;
}

ne10_fft_cpx_float32_t *DopplerAddWin_CalFft(ne10_fft_cpx_float32_t *inBuf, int xsize, int ysize, int zsize, int nfft)
{
	int x, y, z;
	int srcoff, dstoff;

	ne10_fft_r2c_cfg_float32_t cfg;
	ne10_fft_cpx_float32_t *in, *out;
	ne10_fft_cpx_float32_t *pin;
	ne10_fft_cpx_float32_t *pout;

	ne10_fft_cpx_float32_t *outBuf = malloc(xsize * nfft * zsize * sizeof(ne10_fft_cpx_float32_t));
    double *winBuf = malloc((ysize + 1) * sizeof(double));

	cfg = ne10_fft_alloc_r2c_float32(nfft);
	in = (ne10_fft_cpx_float32_t*) malloc(sizeof(ne10_fft_cpx_float32_t) * nfft);
	out = (ne10_fft_cpx_float32_t*) malloc(sizeof(ne10_fft_cpx_float32_t) * nfft);
	if (outBuf == NULL || winBuf == NULL || cfg == NULL || in == NULL || out == NULL)
	{
		if (cfg != NULL)
			ne10_fft_destroy_r2c_float32(cfg);
		free(outBuf);
		free(winBuf);
		free(in);
		free(out);
		return NULL;
	}

    hann(winBuf, ysize + 1, 0);

	for (z = 0; z < zsize; z ++)
	{
		for (x = 0; x < xsize; x ++)
		{
			pin = (ne10_fft_cpx_float32_t *)in;
			pout = (ne10_fft_cpx_float32_t *)out;
			for (y = 0; y < ysize; y ++)
			{
				srcoff = Get3DimArrayOff(x, y, z, xsize, ysize, zsize);
				pin[y].r = (float)winBuf[y + 1] * inBuf[srcoff].r;
                pin[y].i = (float)winBuf[y + 1] * inBuf[srcoff].i;
			}
			for (; y < nfft; y ++)
			{
				pin[y].r = 0.0f;
                pin[y].i = 0.0f;
			}
			ne10_fft_c2c_1d_float32_mxu_ai(out, in, cfg); // 执行变换
			for (y = 0; y < nfft; y ++)
			{
				dstoff = Get3DimArrayOff(x, y, z, xsize, nfft, zsize);
				outBuf[dstoff] = pout[y];
			}
		}
	}

	ne10_fft_destroy_r2c_float32(cfg);
	free(in);
	free(out);

    free(winBuf);
	return outBuf;
}

ne10_fft_cpx_float32_t *AoaAddWin_CalFft(ne10_fft_cpx_float32_t *inBuf, int size ,int nfft, WindowType_t winType)
{
	int y;

	ne10_fft_r2c_cfg_float32_t cfg;
	ne10_fft_cpx_float32_t *in, *out;
	ne10_fft_cpx_float32_t *pin;
	ne10_fft_cpx_float32_t *pout;

	ne10_fft_cpx_float32_t *outBuf = malloc(nfft * sizeof(ne10_fft_cpx_float32_t));
    double *winBuf = malloc((size + 1) * sizeof(double));

    if(winType == HANNING)
    {
        hann(winBuf, size + 1, 0);
    }
    else if (winType == CHEBWIN)
    {
        chebwin(winBuf, size + 1, 0);
    }
    else  // RECTANGULAR
    {
        rectwin(winBuf, size + 1);
    }

	cfg = ne10_fft_alloc_r2c_float32(nfft);
	in = (ne10_fft_cpx_float32_t*) malloc(sizeof(ne10_fft_cpx_float32_t) * nfft);
	out = (ne10_fft_cpx_float32_t*) malloc(sizeof(ne10_fft_cpx_float32_t) * nfft);

    pin = (ne10_fft_cpx_float32_t *)in;
    pout = (ne10_fft_cpx_float32_t *)out;
    for (y = 0; y < size; y ++)
    {
        pin[y].r = (float)winBuf[y + 1] * inBuf[y].r;
        pin[y].i = (float)winBuf[y + 1] * inBuf[y].i;
    }
    for (; y < nfft; y ++)
    {
        pin[y].r = 0.0f;
        pin[y].i = 0.0f;
    }
    ne10_fft_c2c_1d_float32_mxu_ai(out, in, cfg); // 执行变换
    for (y = 0; y < nfft; y ++)
    {
        outBuf[y] = pout[y];
    }

	ne10_fft_destroy_r2c_float32(cfg);
	free(in);
	free(out);

    free(winBuf);
	return outBuf;
}

// 动态计算步幅（避免预存储）
static int calculate_stride(int dims[4], int axis)
{
    switch(axis) {
        case 0: return dims[1]*dims[2]*dims[3];
        case 1: return dims[2]*dims[3];
        case 2: return dims[3];
        case 3: return 1;
        default: return 0;
    }
}

/* eg. Permute3Dim(src[x][y][z], [x, y, z], [1, 0, 2]) = dst[y][x][z] */
ne10_fft_cpx_float32_t *Permute4Dim(
	ne10_fft_cpx_float32_t *original,      // 原数组
    int original_dims[4], // 原维度
    int perm_order[4] // 置换顺序
)
{
    // 生成新维度
    int new_dims[4];

    for(int i=0; i<4; i++)
        new_dims[i] = original_dims[perm_order[i]];

    // 计算逆映射
    int inv_perm[4];
    for(int i=0; i<4; i++)
        for(int j=0; j<4; j++)
            if(perm_order[j] == i)
                { inv_perm[i] = j; break; }

    // 创建新数组
    int new_size = new_dims[0]*new_dims[1]*new_dims[2]*new_dims[3];
    ne10_fft_cpx_float32_t *transposed = malloc(new_size * sizeof(ne10_fft_cpx_float32_t));

    // 优化后的四维遍历
    for(int x=0, idx=0; x<new_dims[0]; x++) {
        for(int y=0; y<new_dims[1]; y++) {
            for(int z=0; z<new_dims[2]; z++) {
                for(int w=0; w<new_dims[3]; w++, idx++) {
                    // 动态坐标映射
                    const int original_coords[4] = {
                        (inv_perm[0]==0)?x:(inv_perm[0]==1)?y:(inv_perm[0]==2)?z:w,
                        (inv_perm[1]==0)?x:(inv_perm[1]==1)?y:(inv_perm[1]==2)?z:w,
                        (inv_perm[2]==0)?x:(inv_perm[2]==1)?y:(inv_perm[2]==2)?z:w,
                        (inv_perm[3]==0)?x:(inv_perm[3]==1)?y:(inv_perm[3]==2)?z:w
                    };

                    // 动态计算原索引
                    int original_index = 0;
                    for(int i=0; i<4; i++)
                        original_index += original_coords[i] * calculate_stride(original_dims, i);

                    transposed[idx] = original[original_index];
                }
            }
        }
    }
    return transposed;
}

/* int dims[3]: [x_dim, y_dim, z_dim] */
/* int perm[3]: [0, 1, 2]  [0, 2, 1] */
/* eg. Permute3Dim(src[x][y][z], [x, y, z], [1, 0, 2]) = dst[y][x][z] */
ne10_fft_cpx_float32_t *Permute3Dim(ne10_fft_cpx_float32_t *src, int origDims[3], int permOrder[3])
{
	int newDims[3];

    for (int i = 0; i < 3; i ++)
    {
    	newDims[i] = origDims[permOrder[i]];
    }

    ne10_fft_cpx_float32_t *dst = malloc(newDims[0] * newDims[1] * newDims[2] * sizeof(ne10_fft_cpx_float32_t));
    // 遍历原数组并填充新数组
    for (int i=0, idx = 0; i<origDims[0]; i++)
    {
        for (int j=0; j<origDims[1]; j++)
        {
            for (int k=0; k<origDims[2]; k++, idx++)
            {
            	// 根据排列方式映射索引
            	int val[3] = {i, j, k};
                int new_i = val[permOrder[0]];
                int new_j = val[permOrder[1]];
                int new_k = val[permOrder[2]];

                int new_off = Get3DimArrayOff(new_k, new_j, new_i, newDims[2], newDims[1], newDims[0]);
                dst[new_off] = src[idx];
            }
        }
    }
    return dst;
}

int16_t *Permute4Dim_int16(
	int16_t *original,      // 原数组
    int original_dims[4], // 原维度
    int perm_order[4] // 置换顺序
)
{
    // 生成新维度
    int new_dims[4];

    for(int i=0; i<4; i++)
        new_dims[i] = original_dims[perm_order[i]];

    // 计算逆映射
    int inv_perm[4];
    for(int i=0; i<4; i++)
        for(int j=0; j<4; j++)
            if(perm_order[j] == i)
                { inv_perm[i] = j; break; }

    // 创建新数组
    int new_size = new_dims[0]*new_dims[1]*new_dims[2]*new_dims[3];
    int16_t *transposed = malloc(new_size * sizeof(int16_t));

    // 优化后的四维遍历
    for(int x=0, idx=0; x<new_dims[0]; x++) {
        for(int y=0; y<new_dims[1]; y++) {
            for(int z=0; z<new_dims[2]; z++) {
                for(int w=0; w<new_dims[3]; w++, idx++) {
                    // 动态坐标映射
                    const int original_coords[4] = {
                        (inv_perm[0]==0)?x:(inv_perm[0]==1)?y:(inv_perm[0]==2)?z:w,
                        (inv_perm[1]==0)?x:(inv_perm[1]==1)?y:(inv_perm[1]==2)?z:w,
                        (inv_perm[2]==0)?x:(inv_perm[2]==1)?y:(inv_perm[2]==2)?z:w,
                        (inv_perm[3]==0)?x:(inv_perm[3]==1)?y:(inv_perm[3]==2)?z:w
                    };

                    // 动态计算原索引
                    int original_index = 0;
                    for(int i=0; i<4; i++)
                        original_index += original_coords[i] * calculate_stride(original_dims, i);

                    transposed[idx] = original[original_index];
                }
            }
        }
    }
    return transposed;
}

/* int dims[3]: [x_dim, y_dim, z_dim] */
/* int perm[3]: [0, 1, 2]  [0, 2, 1] */
/* eg. Permute3Dim(src[x][y][z], [x, y, z], [1, 0, 2]) = dst[y][x][z] */
double *Permute3Dim_double(double *src, int origDims[3], int permOrder[3])
{
	int newDims[3];

    for (int i = 0; i < 3; i ++)
    {
    	newDims[i] = origDims[permOrder[i]];
    }

    double *dst = malloc(newDims[0] * newDims[1] * newDims[2] * sizeof(double));
    // 遍历原数组并填充新数组
    for (int i=0, idx = 0; i<origDims[0]; i++)
    {
        for (int j=0; j<origDims[1]; j++)
        {
            for (int k=0; k<origDims[2]; k++, idx++)
            {
            	// 根据排列方式映射索引
            	int val[3] = {i, j, k};
                int new_i = val[permOrder[0]];
                int new_j = val[permOrder[1]];
                int new_k = val[permOrder[2]];

                int new_off = Get3DimArrayOff(new_k, new_j, new_i, newDims[2], newDims[1], newDims[0]);
                dst[new_off] = src[idx];
            }
        }
    }
    return dst;
}

// Find peaks above threshold in 1D array
// data: input 1D FFT array
// data_len: length of the FFT array
// thold: threshold value
// max_num: maximum number of peaks to find
// peakVal: output array for peaks val
// peakLoc: output array for peak indices, base 1
// peakNum: output number of peaks found
// Returns 0 on success, -1 if input length is less than 3
int findPeakAboveThold1d(const ne10_fft_cpx_float32_t *data, int data_len, double thold, int max_num, double **peakVal, int **peakLoc, int *peakNum)
{
    if(data_len < 3)
    {
        return -1;
    }

    double *peakVal_list = malloc(data_len * sizeof(double));
    int *peakLoc_list = malloc(data_len * sizeof(int));
    int Npeak = 0;
    for (int idx = 0; idx < data_len; idx++)
    {
        float curr_val = ComplexAbs(data[idx]);     //cabsf相对ComplexAbs和cabs更快
        float left, right;

        if (idx == 0)
        {
            left = ComplexAbs(data[data_len - 1]);
            right = ComplexAbs(data[1]);
        }
        else if (idx == data_len - 1)
        {
            left = ComplexAbs(data[data_len - 2]);
            right = ComplexAbs(data[0]);
        }
        else
        {
            left = ComplexAbs(data[idx - 1]);
            right = ComplexAbs(data[idx + 1]);
        }

        if (curr_val >= left && curr_val > right && curr_val > thold)
        {
            peakVal_list[Npeak] = curr_val;
            peakLoc_list[Npeak] = idx + 1; // base 1 index
            Npeak++;
        }
    }

    // Sort the peaks by value in descending order
    int *sortIndices = malloc(Npeak * sizeof(int));
    for (int i = 0; i < Npeak; i++)
    {
        sortIndices[i] = i;
    }
    for (int i = 0; i < Npeak - 1; i++)
    {
        for (int j = i + 1; j < Npeak; j++)
        {
            if (peakVal_list[i] < peakVal_list[j])
            {
                // Swap values
                double temp_val = peakVal_list[i];
                peakVal_list[i] = peakVal_list[j];
                peakVal_list[j] = temp_val;

                // Swap indices
                int temp_idx = sortIndices[i];
                sortIndices[i] = sortIndices[j];
                sortIndices[j] = temp_idx;
            }
        }
    }
    if (Npeak <= max_num)
    {
        // If the number of peaks is less than or equal to max_num, return all peaks
        *peakVal = malloc(Npeak * sizeof(double));
        *peakLoc = malloc(Npeak * sizeof(int));
        for (int i = 0; i < Npeak; i++)
        {
            (*peakVal)[i] = peakVal_list[i];
            (*peakLoc)[i] = peakLoc_list[sortIndices[i]];
        }
        *peakNum = Npeak;
        free(peakVal_list);
        free(peakLoc_list);
        free(sortIndices);
        return 0;
    }
    else
    {
        // If more peaks than max_num, return only the top max_num peaks
        *peakVal = malloc(max_num * sizeof(double));
        *peakLoc = malloc(max_num * sizeof(int));
        for (int i = 0; i < max_num; i++)
        {
            (*peakVal)[i] = peakVal_list[i];
            (*peakLoc)[i] = peakLoc_list[sortIndices[i]];
        }
        Npeak = max_num;
        *peakNum = Npeak;
        free(peakVal_list);
        free(peakLoc_list);
        free(sortIndices);
        return 0;
    }
}
