#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "radar_4tx4rx_profile.h"
int main(void)
{
    const float expected_calibration[32] = {
        1.000000f, 0.000000f, -0.935064f, 0.398385f,
        -0.697341f, -0.786336f, 1.191529f, -0.129519f,
        -0.091294f, 1.004782f, -0.278997f, -0.996838f,
        0.881429f, -0.629665f, -0.049072f, 1.230434f,
        0.497096f, 0.775442f, -0.784077f, -0.567465f,
        0.282913f, -0.929807f, 0.674274f, 0.901324f,
        0.022635f, -1.420770f, 0.505457f, 1.368615f,
        -1.150743f, 0.940266f, -0.063695f, -1.674663f,
    };
    /* Independent mm-coordinate oracle from the hardware document. */
    /* Hardware labels TX1..TX4 and RX1..RX4 run right-to-left. */
    const double tx[] = {31.3612,23.5212,15.6812,7.8412};
    const double rx[] = {5.88,3.92,1.96,0};
    assert(radar_4tx4rx_profile_validate() == 0);
    for (unsigned i=0;i<16;i++) {
        float re, im;
        radar_4tx4rx_get_calibration(i, &re, &im);
        assert(fabsf(re-expected_calibration[2*i])<0.00001f);
        assert(fabsf(im-expected_calibration[2*i+1])<0.00001f);
    }
    for (unsigned t=0;t<4;t++) for(unsigned r=0;r<4;r++) {
        assert(fabs(radar_4tx4rx_virtual_position_m(t,r)-(tx[t]+rx[r])*0.001)<1e-8);
        assert(radar_4tx4rx_virtual_index(t,r)==4*t+r);
    }
    assert(isnan(radar_4tx4rx_virtual_position_m(4,0)));
    /* Geometry-to-spatial-frequency-to-angle at both sides of boresight. */
    for(int a=-60;a<=60;a+=15) {
        double rad=a*3.141592653589793/180;
        double f=-0.00196*RADAR_CENTER_FREQUENCY_HZ/299792458.0*sin(rad);
        double recovered=asin(f/radar_4tx4rx_spacing_wavelengths())*180/3.141592653589793;
        assert(fabs(recovered-a)<0.0001);
    }
    puts("GEOMETRY=PASS documented positions/order/spacing/angle-axis; NOT hardware calibration");
    return 0;
}
