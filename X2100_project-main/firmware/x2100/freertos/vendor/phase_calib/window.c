#include "window.h"
#include <math.h>
#include "heap_malloc.h"

#ifndef PI
#define PI 3.14159265358979323846
#endif

void hann(double *win, int n, int sym) {
    if (n <= 1) {
        if (n == 1) win[0] = 1.0;
        return;
    }
    int N = sym ? (n - 1) : n;
    for (int i = 0; i < n; i++) {
        win[i] = 0.5 * (1.0 - cos(2.0 * PI * i / N));
    }
}

void chebwin(double *win, int n, double atten) {
    // Basic fallback; chebwin is not used in the active calibration pipeline.
    for (int i = 0; i < n; i++) {
        win[i] = 1.0;
    }
}

void rectwin(double *win, int n) {
    for (int i = 0; i < n; i++) {
        win[i] = 1.0;
    }
}
