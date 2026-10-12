#ifndef WINDOW_H
#define WINDOW_H

typedef enum {
    HANNING,
    CHEBWIN,
    RECTANGULAR
} WindowType_t;

void hann(double *win, int n, int sym);
void chebwin(double *win, int n, double atten);
void rectwin(double *win, int n);

#endif
