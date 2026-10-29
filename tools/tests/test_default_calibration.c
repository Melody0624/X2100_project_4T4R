#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "board_calib_config.h"

int main(void)
{
    const fft_cpx_f32 expected[8] = {
        {-0.252853f, -0.004765f}, {-0.227882f, -0.269581f},
        {-0.179556f, -0.327274f}, {-0.385240f, -0.138314f},
        {-0.336374f, 0.125028f}, {-0.300888f, -0.327689f},
        {-0.137421f, -0.311057f}, {-0.241690f, -0.061660f}
    };
    assert(BOARD_ANTENNA_CONFIG == BOARD_ANTENNA_2T4R);
    assert(BOARD_TYPE == BOARD_TYPE_BARRACADE);
    assert(sizeof(angCalibMat) == sizeof(expected));
    assert(memcmp(angCalibMat, expected, sizeof(expected)) == 0);
    assert(fabsf(angFFT_interp[0] - (-3.593931f)) < 0.00001f);
    assert(fabsf(angFFT_interp[1] - 0.527261f) < 0.00001f);
    assert(fabsf(angFFT_interp[127] - (-3.943155f)) < 0.00001f);
    for (int i = 0; i < 128; ++i)
        assert(isfinite(angFFT_interp[i]) && fabsf(angFFT_interp[i]) <= 90.0f);
    board_calib_init();
    assert(memcmp(angCalibMat_back, expected, sizeof(expected)) == 0);
    /* A matrix loaded from Flash or produced by active calibration survives
     * backup initialization; the compiled defaults must not overwrite it. */
    angCalibMat[7] = (fft_cpx_f32){2.0f, -3.0f};
    board_calib_init();
    assert(angCalibMat[7].r == 2.0f && angCalibMat[7].i == -3.0f);
    assert(angCalibMat_back[7].r == 2.0f && angCalibMat_back[7].i == -3.0f);
    puts("PASS: raw SDK uses factory eight-channel/angle defaults; backup and runtime overrides preserved");
    return 0;
}
