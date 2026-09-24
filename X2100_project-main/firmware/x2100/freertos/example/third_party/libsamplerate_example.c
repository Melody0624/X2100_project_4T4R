#include <libsamplerate/samplerate.h>
#include <common.h>

#include <math.h>

#ifndef    M_PI
#define    M_PI            3.14159265358979323846264338
#endif

void gen_windowed_sines (int freq_count, const double *freqs, double max, float *output, int output_len)
{    int     k, freq ;
    double    amplitude, phase ;

    amplitude = max / freq_count ;

    for (k = 0 ; k < output_len ; k++)
        output [k] = 0.0 ;

    for (freq = 0 ; freq < freq_count ; freq++)
    {    phase = 0.9 * M_PI / freq_count ;

        if (freqs [freq] <= 0.0 || freqs [freq] >= 0.5)
        {    printf ("\n%s : Error : freq [%d] == %g is out of range. Should be < 0.5.\n", __FILE__, freq, freqs [freq]) ;
            exit (1) ;
            } ;

        for (k = 0 ; k < output_len ; k++)
            output [k] += amplitude * sin (freqs [freq] * (2 * k) * M_PI + phase) ;
        } ;

    /* Apply Hanning Window. */
    for (k = 0 ; k < output_len ; k++)
        output [k] *= 0.5 - 0.5 * cos ((2 * k) * M_PI / (output_len - 1)) ;

    /*    data [k] *= 0.3635819 - 0.4891775 * cos ((2 * k) * M_PI / (output_len - 1))
                    + 0.1365995 * cos ((4 * k) * M_PI / (output_len - 1))
                    - 0.0106411 * cos ((6 * k) * M_PI / (output_len - 1)) ;
        */

    return ;
} /* gen_windowed_sines */

#define BUFFER_LEN 44100
static float input[BUFFER_LEN];
static float output[BUFFER_LEN * 2];

void test(int method)
{
    double freq = 0.01 ;
    gen_windowed_sines (1, &freq, 1.0, input, BUFFER_LEN);

    SRC_DATA src_data;
    src_data.data_in = input;
    src_data.data_out = output;
    src_data.end_of_input = 0;
    src_data.input_frames = BUFFER_LEN;
    src_data.input_frames_used = 0;
    src_data.output_frames = BUFFER_LEN * 2;
    src_data.output_frames_gen = 0;
    src_data.src_ratio = (double)48000 / 44100;

    int error = 0;

    SRC_STATE *src_state = src_new(method, 1, &error);

    printf("begain\n");
    src_process(src_state, &src_data);
    printf("%d %d %d %d\n", method, src_data.input_frames_used, src_data.output_frames_gen, error);
    src_delete(src_state);
}

void test_samplerate(void)
{
    test(SRC_SINC_BEST_QUALITY);
    test(SRC_SINC_MEDIUM_QUALITY);
    test(SRC_SINC_FASTEST);
    test(SRC_ZERO_ORDER_HOLD);
    test(SRC_LINEAR);
}
