#ifndef CALIB_MISC_H
#define CALIB_MISC_H

#include <stdint.h>
#include <math.h>
#include "NE10_dsp.h"
#include "NE10_types.h"
#include "fft.h"
#include "window.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BYTE_ALIGN(size, align) ((((size) + (align) - 1) / (align)) * (align))
#define CALIB_PI acos(-1)

ne10_fft_cpx_float32_t *Fft1D_DiscardHalf(ne10_fft_cpx_float32_t *inBuf, int xsize, int ysize, int zsize);
ne10_fft_cpx_float32_t *DopplerAddWin_CalFft(ne10_fft_cpx_float32_t *inBuf, int xsize, int ysize, int zsize, int nfft);
ne10_fft_cpx_float32_t *RangeAddWin_CalFft(int16_t *inBuf, int xsize, int ysize, int zsize, int nfft);
ne10_fft_cpx_float32_t *AoaAddWin_CalFft(ne10_fft_cpx_float32_t *inBuf, int size ,int nfft, WindowType_t winType);

void CalMaxVal_1Dim(double *inBuf, int32_t num, int32_t *maxIdx, double *maxVal);

void LinspaceDouble(double x1, double x2, int32_t n, double *inLinSpaceBuf);
void UnWrap(double *wrappedPhase, double *unwrappedPhase, int n);

int Interp1Linear(double *pXOrig, int32_t xOrigLen, double *pOrigBuf, double *pXScaler, int32_t xScalerLen, double *pScalerBuf);
int Interp2Linear(double *pXOrig, int32_t xOrigLen, double *pYOrig, int32_t yOrigLen, double *pOriginBuf,
    double *pXScaler, int32_t xScalerLen, double *pYScaler, int32_t yScalerLen, double *pScalerBuf);


int FftShift1Dim(const void *inFftBuf, int32_t rowNum, int32_t colNum, int32_t sizeOfType, void *outShiftBuf);
int FftShift2Dim(const void *inFftBuf, int32_t rowNum, int32_t colNum, int32_t sizeOfType, void *outShiftBuf);

ne10_fft_cpx_float32_t *Permute3Dim(ne10_fft_cpx_float32_t *src, int origDims[3], int permOrder[3]);
ne10_fft_cpx_float32_t *Permute4Dim(ne10_fft_cpx_float32_t *original, int original_dims[4], int perm_order[4]);
int16_t *Permute4Dim_int16(int16_t *original, int original_dims[4], int perm_order[4]);
double *Permute3Dim_double(double *src, int origDims[3], int permOrder[3]);

int CalFftNum(int size);

double Anagle(ne10_fft_cpx_float32_t c);

double AtanDeg(double val);

double Norm2(double *v, int n);

// Inverse sine in degrees
double AsinDeg(double val);

double CosDeg(double val);

double SinDeg(double val);

double Mag2Db(double mag);

double ComplexAbs(ne10_fft_cpx_float32_t c);

int ComplexToDb(ne10_fft_cpx_float32_t *inBuf, int32_t size, double *outDb);

int32_t Get4DimArrayOff(int x, int y, int z, int l, int xsize, int ysize, int zsize, __attribute__((unused))int lsize);

int32_t Get3DimArrayOff(int x, int y, int z, int xsize, int ysize, __attribute__((unused))int zsize);

int32_t Get2DimArrayOff(int x, int y, int xsize, __attribute__((unused))int ysize);

int findPeakAboveThold1d(const ne10_fft_cpx_float32_t *data, int data_len, double thold, int max_num, double **peakVal, int **peakLoc, int *peakNum);

#ifdef __cplusplus
}
#endif

#endif // CALIB_MISC_H
