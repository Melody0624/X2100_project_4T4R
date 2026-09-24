#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "radar_4tx4rx_profile.h"
int main(void)
{
    /* Independent mm-coordinate oracle from the hardware document. */
    const double tx[] = {7.8412,15.6812,23.5212,31.3612};
    const double rx[] = {0,1.96,3.92,5.88};
    assert(radar_4tx4rx_profile_validate() == 0);
    for (unsigned t=0;t<4;t++) for(unsigned r=0;r<4;r++) {
        assert(fabs(radar_4tx4rx_virtual_position_m(t,r)-(tx[t]+rx[r])*0.001)<1e-8);
        assert(radar_4tx4rx_virtual_index(t,r)==4*t+r);
    }
    assert(isnan(radar_4tx4rx_virtual_position_m(4,0)));
    /* Geometry-to-spatial-frequency-to-angle at both sides of boresight. */
    for(int a=-60;a<=60;a+=15) {
        double rad=a*3.141592653589793/180;
        double f=0.00196*RADAR_CENTER_FREQUENCY_HZ/299792458.0*sin(rad);
        double recovered=asin(f/radar_4tx4rx_spacing_wavelengths())*180/3.141592653589793;
        assert(fabs(recovered-a)<0.0001);
    }
    puts("GEOMETRY=PASS documented positions/order/spacing/angle-axis; NOT hardware calibration");
    return 0;
}
