#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "board_calib_config.h"

int main(void)
{
    const fft_cpx_f32 expected[8] = {
        {1.000000f, 0.000000f}, {-1.199234f, 0.340255f},
        {-0.481211f, -0.991089f}, {0.914203f, 0.373536f},
        {0.413993f, 1.169006f}, {-0.884424f, -1.231216f},
        {0.929756f, -1.007362f}, {-0.094489f, 1.232397f}
    };
    assert(BOARD_ANTENNA_CONFIG == BOARD_ANTENNA_2T4R);
    assert(BOARD_TYPE == BOARD_TYPE_BARRACADE);
    assert(sizeof(angCalibMat) == sizeof(expected));
    assert(memcmp(angCalibMat, expected, sizeof(expected)) == 0);
    const double spacing = -0.00196 * 76.5e9 / 299792458.0;
    for (int i = 0; i < 128; ++i) {
        int bin = i < 64 ? i : i - 128;
        double angle = asin(bin / (128.0 * spacing)) * 180.0 / acos(-1.0);
        assert(fabs(angFFT_interp[i] - angle) < 0.00001);
    }
    board_calib_init();
    assert(memcmp(angCalibMat_back, expected, sizeof(expected)) == 0);
    /* A matrix loaded from Flash or produced by active calibration survives
     * backup initialization; the compiled defaults must not overwrite it. */
    angCalibMat[7] = (fft_cpx_f32){2.0f, -3.0f};
    board_calib_init();
    assert(angCalibMat[7].r == 2.0f && angCalibMat[7].i == -3.0f);
    assert(angCalibMat_back[7].r == 2.0f && angCalibMat_back[7].i == -3.0f);
    puts("PASS: raw SDK links user eight-channel defaults and 128 theoretical angles; backup and runtime overrides preserved");
    return 0;
}
