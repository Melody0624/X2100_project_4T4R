/*
 *  Copyright 2014-16 ARM Limited and Contributors.
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *    * Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    * Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
 *    * Neither the name of ARM Limited nor the
 *      names of its contributors may be used to endorse or promote products
 *      derived from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY ARM LIMITED AND CONTRIBUTORS "AS IS" AND
 *  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 *  WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *  DISCLAIMED. IN NO EVENT SHALL ARM LIMITED AND CONTRIBUTORS BE LIABLE FOR ANY
 *  DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 *  (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 *  ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 *  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* license of Kiss FFT */
/*
Copyright (c) 2003-2010, Mark Borgerding

All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
    * Neither the author nor the names of any contributors may be used to endorse or promote products derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

/*
 * NE10 Library : dsp/NE10_rfft_float32.mxuintrinsic.c
 */
#include <msa.h>

#include "NE10_types.h"
#include "NE10_macros.h"
#include "NE10_fft.h"
#include "NE10_dsp.h"
#include "NE10.h"

#ifdef __mips_msa
#include "NE10_rfft_float.msa.h"
#elif defined(__mips_mxu2)
#include "NE10_rfft_float.mxu2.h"
#endif

void dump_vector(v4f32 v)
{
	int i;
	float *f = (float *)&v;

	for(i = 0;i < 4;i++){
		printf("%f,\t",f[i]);
	}
	printf("\n\n");
}

#define DUMP_DATA(v) do{						\
		printf("%s %s ->\n",__FUNCTION__,#v);	\
		dump_vector(v);							\
	}while(0)

NE10_INLINE void ne10_radix8x4_r2c_mxu (ne10_fft_cpx_float32_t *Fout,
										const ne10_fft_cpx_float32_t *Fin,
										const ne10_int32_t fstride,
										const ne10_int32_t mstride,
										const ne10_int32_t nfft,
										ne10_int32_t prescaling)
{
	ne10_int32_t f_count;

    const v4f32 *Fin_mxu  = (v4f32*) Fin;  // 8 x fstride
	v4f32 *Fout_mxu = (v4f32*) Fout; // fstride x 8
	CPLX in[4],out[4];
	ne10_float32_t one_by_N;
	v4f32 one_by_N_mxu;

    (void)mstride;
    (void)in;
    (void)out;

	if(prescaling == 1){
		one_by_N = 0.5 / nfft;
		one_by_N_mxu = FILL_F(one_by_N);
	}
	NE10_DECLARE_8(v4f32,q_in);
    NE10_DECLARE_8(v4f32,q_out);

    for (f_count = fstride; f_count > 0; f_count --)
    {
		NE10_RADIX8x4_R2C_MXU_LOADX(Fin_mxu,q_in,fstride);
#ifdef NE10_DSP_RFFT_SCALING
		if(prescaling == 1){
			NE10_RADIX8x4_C2R_MXU_KERNEL_SCALE_DATA(q_in,one_by_N_mxu);
		}
#endif
        // print q_in0 ~ q_in7
        // NE10_PRINT_Qx8_VECTOR(q_in);

        // do r2c fft, size = 8
        NE10_RADIX8x4_R2C_MXU_KERNEL(q_out,q_in);

        // print q_out0 ~ q_out7
        // NE10_PRINT_Qx8_VECTOR(q_out);

        // store q_out0 ~ q_out7 to Fout_mxu, by step = 1
        NE10_RADIX8x4_R2C_MXU_STORE(Fout_mxu,q_out,1);

        Fin_mxu = Fin_mxu  + 1;
        Fout_mxu += 8; // next column
    }

}



NE10_INLINE void ne10_radix8x4_c2r_mxu (ne10_fft_cpx_float32_t *Fout,
					const ne10_fft_cpx_float32_t *Fin,
					const ne10_int32_t fstride,
					const ne10_int32_t mstride,
					const ne10_int32_t nfft,
					ne10_int32_t prescaling)
{
    ne10_int32_t f_count;

    NE10_DECLARE_8(v4f32,q_in);
    NE10_DECLARE_8(v4f32,q_out);

    ne10_float32_t one_by_N = 0.25 / nfft;

    if(prescaling == 1)
	    one_by_N = 0.5;
    else
	    one_by_N = 0.25 / nfft;

    v4f32 one_by_N_mxu = FILL_F(one_by_N);
    const v4f32 *Fin_mxu  = (v4f32*) Fin;
    v4f32 *Fout_mxu = (v4f32*) Fout;

    for (f_count = fstride; f_count > 0; f_count --)
    {
	    // from Fin_mxu load 8 v4f32 into q_in0 ~ q_in7, by step = 1
	    NE10_RADIX8x4_R2C_MXU_LOAD(Fin_mxu,q_in,1);
	    Fin_mxu += 8;


	    // NE10_PRINT_Qx8_VECTOR(q_in);

	    NE10_RADIX8x4_C2R_MXU_KERNEL(q_out,q_in);

        // NE10_PRINT_Qx8_VECTOR(q_out);

#ifdef NE10_DSP_RFFT_SCALING
		NE10_RADIX8x4_C2R_MXU_KERNEL_SCALE_DATA(q_out,one_by_N_mxu);
#endif
        // store
        NE10_RADIX8x4_R2C_MXU_STOREX(Fout_mxu,q_out,fstride);

        Fout_mxu ++;
    }
}

NE10_INLINE void ne10_radix4x4_r2c_mxu (ne10_fft_cpx_float32_t *Fout,
										const ne10_fft_cpx_float32_t *Fin,
										const ne10_int32_t fstride,
										const ne10_int32_t mstride,
										const ne10_int32_t nfft,
										ne10_int32_t prescaling)
{
    ne10_int32_t f_count;

    const v4f32 *Fin_mxu  = (v4f32*) Fin;
	v4f32 *Fout_mxu = (v4f32*) Fout;
	ne10_float32_t one_by_N;
	v4f32 one_by_N_mxu;

	if(prescaling == 1){
		one_by_N = 0.5 / nfft;
		one_by_N_mxu = FILL_F(one_by_N);
	}
	NE10_DECLARE_4(v4f32,q_in);
	NE10_DECLARE_4(v4f32,q_out);

    for (f_count = 0; f_count < fstride; f_count ++)
    {

        // load
        NE10_RADIX4x4_R2C_MXU_LOADX(Fin_mxu,q_in,fstride);
        NE10_RADIX4x4_R2C_MXU_KERNEL(q_out,q_in);
#ifdef NE10_DSP_RFFT_SCALING
		if(prescaling == 1){
			NE10_RADIX4x4_C2R_MXU_KERNEL_SCALE_DATA(q_in,one_by_N_mxu);
		}
#endif
        // store
        NE10_RADIX4x4_R2C_MXU_STORE(Fout_mxu,q_out,1);

        Fin_mxu = Fin_mxu + 1;
        Fout_mxu += 4;
    }
}

NE10_INLINE void ne10_radix4x4_c2r_mxu (ne10_fft_cpx_float32_t *Fout,
										const ne10_fft_cpx_float32_t *Fin,
										const ne10_int32_t fstride,
										const ne10_int32_t mstride,
										const ne10_int32_t nfft,
										ne10_int32_t prescaling)
{
    ne10_int32_t f_count;

    const v4f32 *Fin_mxu  = (v4f32*) Fin;
          v4f32 *Fout_mxu = (v4f32*) Fout;

    ne10_float32_t one_by_N;
    v4f32 one_by_N_mxu;
	if(prescaling == 1)
	{
		one_by_N = 0.5;
	}else
		one_by_N = 0.25 / nfft;
	one_by_N_mxu = FILL_F(one_by_N);

    for (f_count = 0; f_count < fstride; f_count ++)
    {
        NE10_DECLARE_4(v4f32,q_in);
        NE10_DECLARE_4(v4f32,q_out);

        // load
        NE10_RADIX4x4_R2C_MXU_LOAD(Fin_mxu,q_in,1);
		Fin_mxu += 4;

        // NE10_PRINT_Qx4_VECTOR(q_in);

        NE10_RADIX4x4_C2R_MXU_KERNEL(q_out,q_in);

        // NE10_PRINT_Qx4_VECTOR(q_out);

#ifdef NE10_DSP_RFFT_SCALING
	NE10_RADIX4x4_C2R_MXU_KERNEL_SCALE_DATA(q_out, one_by_N_mxu);
#endif
	// store
        NE10_RADIX4x4_R2C_MXU_STOREX(Fout_mxu,q_out,fstride);
        Fout_mxu ++;
    }
}

NE10_INLINE void ne10_radix4x4_r2c_with_twiddles_first_butterfly_mxu (v4f32 *Fout_mxu,
                                                            const v4f32 *Fin_mxu,
                                                            const ne10_int32_t out_step,
                                                            const ne10_int32_t in_step,
                                                            const ne10_fft_cpx_float32_t *twiddles)
{
	int i;
	float *f = (float*)Fin_mxu;
    NE10_DECLARE_4(v4f32,q_in);
    NE10_DECLARE_4(v4f32,q_out);

    (void)twiddles;
    (void)i;
    (void)f;

    // load
    NE10_RADIX4x4_R2C_MXU_LOADX(Fin_mxu,q_in,in_step);

    NE10_RADIX4x4_R2C_MXU_KERNEL(q_out,q_in);

    // store
	STORE_F(q_out0,Fout_mxu,0);
	STOREX_F(q_out1,Fout_mxu,((out_step << 1) - 1) * MXU_A_SIZE);
	STOREX_F(q_out2,Fout_mxu,(out_step << 1) * MXU_A_SIZE);
	STOREX_F(q_out3,Fout_mxu,(2 * (out_step << 1) - 1) * MXU_A_SIZE);
}

NE10_INLINE void ne10_radix4x4_c2r_with_twiddles_first_butterfly_mxu (v4f32 *Fout_mxu,
                                                            const v4f32 *Fin_mxu,
                                                            const ne10_int32_t out_step,
                                                            const ne10_int32_t in_step,
                                                            const ne10_fft_cpx_float32_t *twiddles)
{
    NE10_DECLARE_4(v4f32,q_in);
    NE10_DECLARE_4(v4f32,q_out);

    // load
    q_in0 = LOAD_F (Fin_mxu,0);
    q_in1 = LOADX_F(Fin_mxu, ((out_step << 1) - 1) * MXU_A_SIZE);
    q_in2 = LOADX_F(Fin_mxu, (out_step << 1) * MXU_A_SIZE);

    q_in3 = LOADX_F(Fin_mxu, ((out_step << 2) - 1) * MXU_A_SIZE);
    // NE10_PRINT_Qx4_VECTOR(q_in);

    NE10_RADIX4x4_C2R_MXU_KERNEL(q_out,q_in);

    // NE10_PRINT_Qx4_VECTOR(q_out);
    // store
    NE10_RADIX4x4_R2C_MXU_STOREX(Fout_mxu,q_out,in_step);
}

NE10_INLINE void ne10_radix4x4_r2c_with_twiddles_other_butterfly_mxu (v4f32 *Fout_mxu,
                                                                const v4f32 *Fin_mxu,
                                                                const ne10_int32_t out_step,
                                                                const ne10_int32_t in_step,
                                                                const ne10_fft_cpx_float32_t *twiddles)
{
    ne10_int32_t m_count;
    ne10_int32_t loop_count = (out_step>>1) -1;
    v4f32 *Fout_b = Fout_mxu + (((out_step<<1)-1)<<1) - 2; // reversed

    NE10_DECLARE_3(CPLX,q2_tw);
    NE10_DECLARE_4(CPLX,q2_in);
    NE10_DECLARE_4(CPLX,q2_out);

    for (m_count = loop_count; m_count > 0; m_count -- )
    {
        // load
		NE10_RADIX4x4_CPLX_LOADX(Fin_mxu,q2_in,in_step);

		NE10_RADIX4x4_CPLX_TW_FILL(twiddles,q2_tw);

        // R2C TW KERNEL
        NE10_RADIX4x4_R2C_TW_MUL_MXU (q2_out, q2_in, q2_tw);

        NE10_RADIX4x4_R2C_TW_MXU_KERNEL_S1 (q2_in, q2_out);
        NE10_RADIX4x4_R2C_TW_MXU_KERNEL_S2 (q2_out, q2_in);

        // store
		CPLX_STORE(q2_out0,Fout_mxu,0);
		CPLX_STOREX(q2_out1,Fout_mxu,(out_step << 1) * MXU_A_SIZE);

		CPLX_STORE(q2_out2,Fout_b,0);
		CPLX_STOREX(q2_out3,Fout_b,-(out_step << 1) * MXU_A_SIZE);
        // update pointers
        Fin_mxu  += 2;
        Fout_mxu += 2;
        Fout_b    -= 2;
        twiddles += 3;
    }
}

NE10_INLINE void ne10_radix4x4_c2r_with_twiddles_other_butterfly_mxu (v4f32 *Fout_mxu,
                                                                const v4f32 *Fin_mxu,
                                                                const ne10_int32_t out_step,
                                                                const ne10_int32_t in_step,
                                                                const ne10_fft_cpx_float32_t *twiddles)
{
    ne10_int32_t m_count;
    ne10_int32_t loop_count = (out_step>>1) -1;
    const v4f32 *Fin_b = Fin_mxu + (((out_step<<1)-1)<<1) - 2; // reversed

    NE10_DECLARE_3(CPLX,q2_tw);
    NE10_DECLARE_4(CPLX,q2_in);
    NE10_DECLARE_4(CPLX,q2_out);

    for (m_count = loop_count; m_count > 0; m_count -- )
    {
        // load
		CPLX_LOAD(q2_in0,Fin_mxu,0);
		CPLX_LOADX(q2_in1,Fin_mxu,(out_step << 1) * MXU_A_SIZE);

		CPLX_LOAD(q2_in2,Fin_b,0);
		CPLX_LOADX(q2_in3,Fin_b,-(out_step << 1) * MXU_A_SIZE);

		NE10_RADIX4x4_CPLX_TW_FILL(twiddles,q2_tw);

        // NE10_PRINT_Q2x4_VECTOR(q2_in);

        // R2C TW KERNEL
        NE10_RADIX4x4_C2R_TW_MXU_KERNEL(q2_out,q2_in,q2_tw);

        // NE10_PRINT_Q2x4_VECTOR(q2_out);

        // store
        // update pointers
		NE10_RADIX4x4_CPLX_STOREX(Fout_mxu,q2_out,in_step);
        Fin_mxu  += 2;
        Fout_mxu += 2;
        Fin_b    -= 2;
        twiddles += 3;
    }
}


NE10_INLINE void ne10_radix4x4_r2c_with_twiddles_last_butterfly_mxu (v4f32 *Fout_mxu,
                                                            const v4f32 *Fin_mxu,
                                                            const ne10_int32_t out_step,
                                                            const ne10_int32_t in_step,
                                                            const ne10_fft_cpx_float32_t *twiddles)
{
    NE10_DECLARE_4(v4f32,q_in);
    NE10_DECLARE_4(v4f32,q_out);

    // load
    NE10_RADIX4x4_R2C_MXU_LOADX(Fin_mxu,q_in,in_step);

    NE10_RADIX4x4_R2C_TW_MXU_KERNEL_LAST(q_out,q_in);

    // store
	STORE_F(q_out0,Fout_mxu,0);
	STORE_F(q_out1,Fout_mxu,1 * MXU_A_SIZE);
	STOREX_F(q_out2,Fout_mxu,(out_step << 1) * MXU_A_SIZE);
	STOREX_F(q_out3,Fout_mxu,((out_step << 1) + 1) * MXU_A_SIZE);
}

NE10_INLINE void ne10_radix4x4_c2r_with_twiddles_last_butterfly_mxu (v4f32 *Fout_mxu,
                                                            const v4f32 *Fin_mxu,
                                                            const ne10_int32_t out_step,
                                                            const ne10_int32_t in_step,
                                                            const ne10_fft_cpx_float32_t *twiddles)
{
    NE10_DECLARE_4(v4f32,q_in);
    NE10_DECLARE_4(v4f32,q_out);

    // load
	q_in0 = LOAD_F(Fin_mxu,0);
	q_in1 = LOAD_F(Fin_mxu,1 * MXU_A_SIZE);
	q_in2 = LOADX_F(Fin_mxu,(out_step << 1) * MXU_A_SIZE);
	q_in3 = LOADX_F(Fin_mxu,((out_step << 1) + 1) * MXU_A_SIZE);

    // NE10_PRINT_Qx4_VECTOR(q_in);

    NE10_RADIX4x4_C2R_TW_MXU_KERNEL_LAST(q_out,q_in);

    // NE10_PRINT_Qx4_VECTOR(q_out);

    // store
    NE10_RADIX4x4_R2C_MXU_STOREX(Fout_mxu,q_out,in_step);
}

NE10_INLINE void ne10_radix4x4_r2c_with_twiddles_mxu (ne10_fft_cpx_float32_t *Fout,
                                                        const ne10_fft_cpx_float32_t *Fin,
                                                        const ne10_int32_t fstride,
                                                        const ne10_int32_t mstride,
                                                        const ne10_int32_t nfft,
                                                        const ne10_fft_cpx_float32_t *twiddles)
{
    ne10_int32_t f_count;
    const ne10_int32_t in_step = nfft >> 2;
    const ne10_int32_t out_step = mstride;

    const v4f32 *Fin_mxu  = (v4f32*) Fin;
          v4f32 *Fout_mxu = (v4f32*) Fout;
    const ne10_fft_cpx_float32_t *tw;

    for (f_count = fstride; f_count; f_count --)
    {
        tw = twiddles + 3;
        // first butterfly
        ne10_radix4x4_r2c_with_twiddles_first_butterfly_mxu ( Fout_mxu, Fin_mxu, out_step, in_step, NULL);

        Fin_mxu ++;
        Fout_mxu ++;

        // other butterfly
        // Twiddle tables are transposed to avoid memory access by a large stride.
        ne10_radix4x4_r2c_with_twiddles_other_butterfly_mxu ( Fout_mxu, Fin_mxu, out_step, in_step, tw);
        // update Fin_r, Fout_r, twiddles
        Fin_mxu  += 2 * ( (out_step >> 1) - 1);
        Fout_mxu += 2 * ( (out_step >> 1) - 1);

        // last butterfly
        ne10_radix4x4_r2c_with_twiddles_last_butterfly_mxu (Fout_mxu, Fin_mxu, out_step, in_step, NULL);
        Fin_mxu ++;
        Fout_mxu ++;

        Fout_mxu = Fout_mxu + 3 * out_step;
    } // f_count

}

NE10_INLINE void ne10_radix4x4_c2r_with_twiddles_mxu (ne10_fft_cpx_float32_t *Fout,
                                                        const ne10_fft_cpx_float32_t *Fin,
                                                        const ne10_int32_t fstride,
                                                        const ne10_int32_t mstride,
                                                        const ne10_int32_t nfft,
                                                        const ne10_fft_cpx_float32_t *twiddles)
{
    ne10_int32_t f_count;
    const ne10_int32_t in_step = nfft >> 2;
    const ne10_int32_t out_step = mstride;

    const v4f32 *Fin_mxu  = (v4f32*) Fin;
          v4f32 *Fout_mxu = (v4f32*) Fout;
    const ne10_fft_cpx_float32_t *tw;

    for (f_count = fstride; f_count; f_count --)
    {
        tw = twiddles + 3;

        // first butterfly
        ne10_radix4x4_c2r_with_twiddles_first_butterfly_mxu ( Fout_mxu, Fin_mxu, out_step, in_step, NULL);

        Fin_mxu ++;
        Fout_mxu ++;

        // other butterfly
        // Twiddle tables are transposed to avoid memory access by a large stride.
        ne10_radix4x4_c2r_with_twiddles_other_butterfly_mxu ( Fout_mxu, Fin_mxu, out_step, in_step, tw);

        // update Fin_r, Fout_r, twiddles
        Fin_mxu  += 2 * ( (out_step >> 1) - 1);
        Fout_mxu += 2 * ( (out_step >> 1) - 1);

        // last butterfly
        ne10_radix4x4_c2r_with_twiddles_last_butterfly_mxu (Fout_mxu, Fin_mxu, out_step, in_step, NULL);
        Fin_mxu ++;
        Fout_mxu ++;

        Fin_mxu = Fin_mxu + 3 * out_step;
    } // f_count
}

NE10_INLINE void ne10_mixed_radix_r2c_butterfly_float32_mxu (ne10_fft_cpx_float32_t * Fout,
                                                        const ne10_fft_cpx_float32_t * Fin,
                                                        const ne10_int32_t * factors,
                                                        const ne10_fft_cpx_float32_t * twiddles,
                                                        ne10_fft_cpx_float32_t * buffer)
{
    ne10_int32_t fstride, mstride, nfft,prescaling;
    ne10_int32_t radix;
    ne10_int32_t stage_count;

    // PRINT_STAGE_INFO;

    // init fstride, mstride, radix, nfft
    stage_count = factors[0];
    fstride     = factors[1];
    mstride     = factors[ (stage_count << 1) - 1 ];
    radix       = factors[  stage_count << 1 ];
    nfft        = radix * fstride; // not the real nfft
	prescaling = factors[NE10_MAXFACTORS * 2 - 1];

    // PRINT_STAGE_INFO;
    if (stage_count % 2 == 1) // since there is another stage outside
    {
        ne10_swap_ptr (buffer, Fout);
    }

    // the first stage
    if (radix == 8)   // length of FFT is 2^n (n is odd)
    {
        ne10_radix8x4_r2c_mxu (Fout, Fin, fstride, mstride, nfft, prescaling);
    }
    else if (radix == 4)   // length of FFT is 2^n (n is even)
    {
        ne10_radix4x4_r2c_mxu (Fout, Fin, fstride, mstride, nfft, prescaling);
    }
    // end of first stage

    // others
    for (; fstride > 1;)
    {
        fstride >>= 2;
        ne10_swap_ptr (buffer, Fout);

        ne10_radix4x4_r2c_with_twiddles_mxu (Fout, buffer, fstride, mstride, nfft, twiddles);
        twiddles += 3 * mstride;
        mstride <<= 2;
    } // other stage
}

NE10_INLINE void ne10_mixed_radix_c2r_butterfly_float32_mxu (ne10_fft_cpx_float32_t * Fout,
                                                        const ne10_fft_cpx_float32_t * Fin,
                                                        const ne10_int32_t * factors,
                                                        const ne10_fft_cpx_float32_t * twiddles,
                                                        ne10_fft_cpx_float32_t * buffer)
{
    ne10_int32_t fstride, mstride, nfft, prescaling;
    ne10_int32_t radix;
    ne10_int32_t stage_count;

    // PRINT_STAGE_INFO;

    // init fstride, mstride, radix, nfft
    stage_count = factors[0];
    fstride     = factors[1];

    mstride     = factors[ (stage_count << 1) - 1 ];
    radix       = factors[  stage_count << 1 ];
    nfft        = radix * fstride; // not the real nfft
	 prescaling = factors[NE10_MAXFACTORS * 2 - 1];
    // fstride, mstride for last last stage
    fstride = 1;
    mstride = nfft >> 2;

    if (stage_count % 2 == 0)
    {
        ne10_swap_ptr(Fout,buffer);
    }

    // others but the first stage
    for (; stage_count > 1;)
    {
        twiddles -= 3 * mstride;

        // PRINT_STAGE_INFO;
        // PRINT_POINTERS_INFO(Fin,Fout,buffer,twiddles);
        ne10_radix4x4_c2r_with_twiddles_mxu (Fout, buffer, fstride, mstride, nfft, twiddles);

        fstride <<= 2;
        mstride >>= 2;
        stage_count --;
        ne10_swap_ptr (buffer, Fout);
    }
    // first stage -- inversed
    if (radix == 8)   // length of FFT is 2^n (n is odd)
    {
        // PRINT_STAGE_INFO;
        // PRINT_POINTERS_INFO(Fin,Fout,buffer,twiddles);
        ne10_radix8x4_c2r_mxu (Fout, buffer, fstride, mstride, nfft, prescaling);
    }
    else if (radix == 4)   // length of FFT is 2^n (n is even)
    {
        // PRINT_STAGE_INFO;
        // PRINT_POINTERS_INFO(Fin,Fout,buffer,twiddles);
        ne10_radix4x4_c2r_mxu (Fout, buffer, fstride, mstride, nfft, prescaling);
    }
}

NE10_INLINE void ne10_radix4_r2c_with_twiddles_last_stage_first_butterfly (ne10_fft_cpx_float32_t *dst,
                                            const ne10_fft_cpx_float32_t *src,
                                            const ne10_fft_cpx_float32_t *twiddles,
                                            const ne10_int32_t nfft)
{
    // b0
    {
        ne10_float32_t q_4r_out[4];
        const ne10_float32_t *p_src_r = (const ne10_float32_t*) src;

        NE10_FFT_R2C_4R_RCR(q_4r_out,p_src_r);

        dst[0].r = q_4r_out[0];
        dst[0].i = q_4r_out[3];
        dst += (nfft>>2);
        dst[0].r = q_4r_out[1];
        dst[0].i = q_4r_out[2];
        dst -= (nfft>>2);
    }

    // b2
    {
        const ne10_float32_t *p_src_r = (const ne10_float32_t*) (src);
        p_src_r  += nfft;
        p_src_r  -= 4;

        ne10_float32_t q_4r_out[4];

        NE10_FFT_R2C_4R_CC(q_4r_out,p_src_r);

        dst += (nfft>>3);
        dst[0].r = q_4r_out[0];
        dst[0].i = q_4r_out[1];
        dst += (nfft>>2);
        dst[0].r = q_4r_out[2];
        dst[0].i = q_4r_out[3];
        dst -= (nfft>>3);
        dst -= (nfft>>2);
    }

    // b1
    ne10_fft_cpx_float32_t cc_out[4];
    ne10_fft_cpx_float32_t cc_in [4];
    const ne10_float32_t *p_src_r = (const ne10_float32_t*) src;
    p_src_r += 4;

    cc_out[0].r = *(p_src_r ++);
    cc_out[1].r = *(p_src_r ++);
    cc_out[2].r = *(p_src_r ++);
    cc_out[3].r = *(p_src_r ++);

    cc_out[0].i = *(p_src_r ++);
    cc_out[1].i = *(p_src_r ++);
    cc_out[2].i = *(p_src_r ++);
    cc_out[3].i = *(p_src_r ++);

    NE10_PRINT_Q2_VECTOR(cc_out);

    // twiddles[0] = ( 1.0, 0.0);
    // NE10_CPX_MUL_F32(cc_in[0],cc_out[0],twiddles[0]);
    cc_in[0] = cc_out[0];
    twiddles ++;

    NE10_CPX_MUL_F32(cc_in[1],cc_out[1],twiddles[0]);
    twiddles ++;

    NE10_CPX_MUL_F32(cc_in[2],cc_out[2],twiddles[0]);
    twiddles ++;

    NE10_CPX_MUL_F32(cc_in[3],cc_out[3],twiddles[0]);

    // NE10_PRINT_Q2_VECTOR(cc_in);

    NE10_FFT_R2C_CC_CC(cc_out,cc_in);

    // NE10_PRINT_Q2_VECTOR(cc_out);

    dst[1] = cc_out[0];
    dst += (nfft>>2);
    dst[ 1] = cc_out[1];
    dst[-1] = cc_out[3];
    dst += (nfft>>2);
    dst[-1] = cc_out[2];
}

NE10_INLINE void ne10_radix4_c2r_with_twiddles_first_stage_first_butterfly (ne10_fft_cpx_float32_t *dst,
                                            const ne10_fft_cpx_float32_t *src,
                                            const ne10_fft_cpx_float32_t *twiddles,
                                            const ne10_int32_t nfft)
{
    // b0
    {
        ne10_float32_t q_4r_in[4];
        ne10_float32_t *p_dst_r = (ne10_float32_t*) dst;

        q_4r_in[0] = src[0].r;
        q_4r_in[3] = src[0].i;
        src += (nfft>>2);
        q_4r_in[1] = src[0].r;
        q_4r_in[2] = src[0].i;
        src -= (nfft>>2);

        NE10_FFT_C2R_RCR_4R(p_dst_r,q_4r_in);
    }

    // b2
    {
        // v4f32 q_in;
        ne10_float32_t *p_dst_r = (ne10_float32_t*) (dst);
        p_dst_r  += nfft;
        p_dst_r  -= 4;

        ne10_float32_t q_4r_in[4];
        src += (nfft>>3);
        q_4r_in[0] = src[0].r;
        q_4r_in[1] = src[0].i;
        src += (nfft>>2);
        q_4r_in[2] = src[0].r;
        q_4r_in[3] = src[0].i;
        src -= (nfft>>3);
        src -= (nfft>>2);

        NE10_FFT_C2R_CC_4R(p_dst_r,q_4r_in);
    }

    // b1
    ne10_fft_cpx_float32_t cc_out[4];
    ne10_fft_cpx_float32_t cc_in [4];
    ne10_float32_t *p_dst_r = (ne10_float32_t*) dst;
    p_dst_r += 4;

    // load
    cc_out[0] = src[1];
    src += (nfft>>2);
    cc_out[2] = src[ 1];
    cc_out[3] = src[-1];
    src += (nfft>>2);
    cc_out[1] = src[-1];

    // NE10_PRINT_Q2_VECTOR(cc_out);

    NE10_FFT_C2R_CC_CC(cc_in,cc_out);

    // NE10_PRINT_Q2_VECTOR(cc_in);

    // twiddles[0] = ( 1.0, 0.0);
    // NE10_CPX_MUL_F32(cc_in[0],cc_out[0],twiddles[0]);
    cc_out[0] = cc_in[0];
    twiddles ++;

    NE10_CPX_CONJ_MUL_F32(cc_out[1],cc_in[1],twiddles[0]);
    twiddles ++;

    NE10_CPX_CONJ_MUL_F32(cc_out[2],cc_in[2],twiddles[0]);
    twiddles ++;

    NE10_CPX_CONJ_MUL_F32(cc_out[3],cc_in[3],twiddles[0]);

    // NE10_PRINT_Q2_VECTOR(cc_out);

    *(p_dst_r ++) = cc_out[0].r;
    *(p_dst_r ++) = cc_out[1].r;
    *(p_dst_r ++) = cc_out[2].r;
    *(p_dst_r ++) = cc_out[3].r;

    *(p_dst_r ++) = cc_out[0].i;
    *(p_dst_r ++) = cc_out[1].i;
    *(p_dst_r ++) = cc_out[2].i;
    *(p_dst_r ++) = cc_out[3].i;
}

NE10_INLINE void ne10_radix4_r2c_with_twiddles_last_stage_second_butterfly (ne10_fft_cpx_float32_t *dst,
                                            const ne10_fft_cpx_float32_t *src,
                                            const ne10_fft_cpx_float32_t *twiddles,
                                            const ne10_int32_t nfft)
{
    // assert ( nfft % 4 == 0 );
    const ne10_float32_t *fin_r  = (const ne10_float32_t*) src + 12;
          ne10_float32_t *fout_r =       (ne10_float32_t*) dst;
    const ne10_float32_t *tw     = (const ne10_float32_t*) twiddles + 8;

    ne10_float32_t q_in0[4],    q_out0[4],
                   q_in1[4],    q_out1[4],
                   q_in2[4],    q_out2[4],
                   q_in3[4],    q_out3[4];

    ne10_float32_t q2_tw0[2][4],
                   q2_tw1[2][4];

    /*  INPUT & OUTPUT
     *  0R  1R  2R  3R      Q0
     *  0I  1I  2I  3I      Q1
     *  4R  5R  6R  7R      Q2
     *  4I  5I  6I  7I      Q3
     */

    q_in0[0] = *(fin_r++);
    q_in0[1] = *(fin_r++);
    q_in0[2] = *(fin_r++);
    q_in0[3] = *(fin_r++);
    q_in1[0] = *(fin_r++);
    q_in1[1] = *(fin_r++);
    q_in1[2] = *(fin_r++);
    q_in1[3] = *(fin_r++);
    q_in2[0] = *(fin_r++);
    q_in2[1] = *(fin_r++);
    q_in2[2] = *(fin_r++);
    q_in2[3] = *(fin_r++);
    q_in3[0] = *(fin_r++);
    q_in3[1] = *(fin_r++);
    q_in3[2] = *(fin_r++);
    q_in3[3] = *(fin_r++);

    // NE10_PRINT_Q_VECTOR(q_in0);
    // NE10_PRINT_Q_VECTOR(q_in1);
    // NE10_PRINT_Q_VECTOR(q_in2);
    // NE10_PRINT_Q_VECTOR(q_in3);

    q2_tw0[0][0] = tw[0];
    q2_tw0[0][1] = tw[2];
    q2_tw0[0][2] = tw[4];
    q2_tw0[0][3] = tw[6];
    q2_tw0[1][0] = tw[1];
    q2_tw0[1][1] = tw[3];
    q2_tw0[1][2] = tw[5];
    q2_tw0[1][3] = tw[7];

    q2_tw1[0][0] = tw[0+8];
    q2_tw1[0][1] = tw[2+8];
    q2_tw1[0][2] = tw[4+8];
    q2_tw1[0][3] = tw[6+8];
    q2_tw1[1][0] = tw[1+8];
    q2_tw1[1][1] = tw[3+8];
    q2_tw1[1][2] = tw[5+8];
    q2_tw1[1][3] = tw[7+8];

    // TW: in->out
    q_out0[0] = q_in0[0];
    q_out1[0] = q_in1[0];
    q_out2[0] = q_in2[0];
    q_out3[0] = q_in3[0];

    //----------------------------------------------------------//
    // first 2 lines
    //   R          R             R           I             I
    q_out0[1] = q_in0[1] * q2_tw0[0][1] - q_in1[1] * q2_tw0[1][1];
    //   I          R             I           I             R
    q_out1[1] = q_in0[1] * q2_tw0[1][1] + q_in1[1] * q2_tw0[0][1];

    //   R          R             R           I             I
    q_out0[2] = q_in0[2] * q2_tw0[0][2] - q_in1[2] * q2_tw0[1][2];
    //   I          R             I           I             R
    q_out1[2] = q_in0[2] * q2_tw0[1][2] + q_in1[2] * q2_tw0[0][2];

    //   R          R             R           I             I
    q_out0[3] = q_in0[3] * q2_tw0[0][3] - q_in1[3] * q2_tw0[1][3];
    //   I          R             I           I             R
    q_out1[3] = q_in0[3] * q2_tw0[1][3] + q_in1[3] * q2_tw0[0][3];

    //---------------------------------------------------------//
    // second 2 lines
    //   R          R             R           I             I
    q_out2[1] = q_in2[1] * q2_tw1[0][1] - q_in3[1] * q2_tw1[1][1];
    //   I          R             I           I             R
    q_out3[1] = q_in2[1] * q2_tw1[1][1] + q_in3[1] * q2_tw1[0][1];

    //   R          R             R           I             I
    q_out2[2] = q_in2[2] * q2_tw1[0][2] - q_in3[2] * q2_tw1[1][2];
    //   I          R             I           I             R
    q_out3[2] = q_in2[2] * q2_tw1[1][2] + q_in3[2] * q2_tw1[0][2];

    //   R          R             R           I             I
    q_out2[3] = q_in2[3] * q2_tw1[0][3] - q_in3[3] * q2_tw1[1][3];
    //   I          R             I           I             R
    q_out3[3] = q_in2[3] * q2_tw1[1][3] + q_in3[3] * q2_tw1[0][3];

    // NE10_PRINT_Q_VECTOR(q_out0);
    // NE10_PRINT_Q_VECTOR(q_out1);
    // NE10_PRINT_Q_VECTOR(q_out2);
    // NE10_PRINT_Q_VECTOR(q_out3);

    // BUTTERFLY - radix 4x2
    // STAGE
    // q_out -> q_in
    //  R i         R j         R k
    q_in0[0] = q_out0[0] + q_out0[2];
    q_in1[0] = q_out1[0] + q_out1[2];

    q_in0[1] = q_out0[0] - q_out0[2];
    q_in1[1] = q_out1[0] - q_out1[2];

    //  R i         R j         R k
    q_in0[2] = q_out0[1] + q_out0[3];
    q_in1[2] = q_out1[1] + q_out1[3];

    q_in0[3] = q_out0[1] - q_out0[3];
    q_in1[3] = q_out1[1] - q_out1[3];

    //  R i         R j         R k
    q_in2[0] = q_out2[0] + q_out2[2];
    q_in3[0] = q_out3[0] + q_out3[2];

    q_in2[1] = q_out2[0] - q_out2[2];
    q_in3[1] = q_out3[0] - q_out3[2];

    //  R i         R j         R k
    q_in2[2] = q_out2[1] + q_out2[3];
    q_in3[2] = q_out3[1] + q_out3[3];

    q_in2[3] = q_out2[1] - q_out2[3];
    q_in3[3] = q_out3[1] - q_out3[3];

    // NE10_PRINT_Q_VECTOR(q_in0);
    // NE10_PRINT_Q_VECTOR(q_in1);
    // NE10_PRINT_Q_VECTOR(q_in2);
    // NE10_PRINT_Q_VECTOR(q_in3);

    // STAGE
    // q_in -> q_out
    // and transpose
    //   R i          R j        R k
    q_out0[0] =   q_in0[0] + q_in0[2];
    q_out0[1] =   q_in1[0] + q_in1[2];

    q_out2[2] =   q_in0[0] - q_in0[2];
    q_out2[3] = - q_in1[0] + q_in1[2];// CONJ

    //   R i          R j        R k
    q_out3[2] =   q_in0[1] - q_in1[3];
    q_out3[3] = - q_in1[1] - q_in0[3];// CONJ

    q_out1[0] =   q_in0[1] + q_in1[3];
    q_out1[1] =   q_in1[1] - q_in0[3];

    //   R i          R j        R k
    q_out0[2] =   q_in2[0] + q_in2[2];
    q_out0[3] =   q_in3[0] + q_in3[2];

    q_out2[0] =   q_in2[0] - q_in2[2];
    q_out2[1] = - q_in3[0] + q_in3[2];// CONJ

    //   R i          R j        R k
    q_out3[0] =   q_in2[1] - q_in3[3];
    q_out3[1] = - q_in3[1] - q_in2[3]; // CONJ

    q_out1[2] =   q_in2[1] + q_in3[3];
    q_out1[3] =   q_in3[1] - q_in2[3];

    // NE10_PRINT_Q_VECTOR(q_out0);
    // NE10_PRINT_Q_VECTOR(q_out1);
    // NE10_PRINT_Q_VECTOR(q_out2);
    // NE10_PRINT_Q_VECTOR(q_out3);

    // STORE
    fout_r += 4;
    fout_r[0] = q_out0[0];
    fout_r[1] = q_out0[1];
    fout_r[2] = q_out0[2];
    fout_r[3] = q_out0[3];

    fout_r += (nfft>>1);
    fout_r[0] = q_out1[0];
    fout_r[1] = q_out1[1];
    fout_r[2] = q_out1[2];
    fout_r[3] = q_out1[3];

    fout_r -= 10;
    fout_r[0] = q_out3[0];
    fout_r[1] = q_out3[1];
    fout_r[2] = q_out3[2];
    fout_r[3] = q_out3[3];

    fout_r += (nfft>>1);
    fout_r[0] = q_out2[0];
    fout_r[1] = q_out2[1];
    fout_r[2] = q_out2[2];
    fout_r[3] = q_out2[3];
}

NE10_INLINE void ne10_radix4_c2r_with_twiddles_first_stage_second_butterfly (ne10_fft_cpx_float32_t *dst,
                                            const ne10_fft_cpx_float32_t *src,
                                            const ne10_fft_cpx_float32_t *twiddles,
                                            const ne10_int32_t nfft)
{
    const ne10_float32_t *fin_r  = (const ne10_float32_t*) src;
          ne10_float32_t *fout_r =       (ne10_float32_t*) dst + 12;
    const ne10_float32_t *tw     = (const ne10_float32_t*) twiddles + 8;

    ne10_float32_t q_in0[4],    q_out0[4],
                   q_in1[4],    q_out1[4],
                   q_in2[4],    q_out2[4],
                   q_in3[4],    q_out3[4];

    ne10_float32_t q2_tw0[2][4],
                   q2_tw1[2][4];

    /*  INPUT & OUTPUT
     *  0R  1R  2R  3R      Q0
     *  0I  1I  2I  3I      Q1
     *  4R  5R  6R  7R      Q2
     *  4I  5I  6I  7I      Q3
     */

    // load
    fin_r += 4;
    q_in0[0] = fin_r[0];
    q_in0[1] = fin_r[1];
    q_in0[2] = fin_r[2];
    q_in0[3] = fin_r[3];

    fin_r += (nfft>>1);
    q_in1[0] = fin_r[0];
    q_in1[1] = fin_r[1];
    q_in1[2] = fin_r[2];
    q_in1[3] = fin_r[3];

    fin_r -= 10;
    q_in3[0] = fin_r[0];
    q_in3[1] = fin_r[1];
    q_in3[2] = fin_r[2];
    q_in3[3] = fin_r[3];

    fin_r += (nfft>>1);
    q_in2[0] = fin_r[0];
    q_in2[1] = fin_r[1];
    q_in2[2] = fin_r[2];
    q_in2[3] = fin_r[3];

    // NE10_PRINT_Q_VECTOR(q_in0);
    // NE10_PRINT_Q_VECTOR(q_in1);
    // NE10_PRINT_Q_VECTOR(q_in2);
    // NE10_PRINT_Q_VECTOR(q_in3);

    // OUTPUT
    // INPUT
#define NE10_INV_BUTTERFLY_TMP(I1,I2,J1,J2,K1,K2,S1,S2) do {    \
    q_out ## I1 [I2] = ( q_in ## K1 [K2] + q_in ## S1 [S2] ); \
    q_out ## J1 [J2] = ( q_in ## K1 [K2] - q_in ## S1 [S2] ); \
} while(0);

    // STAGE
    // q_in -> q_out
    // and transpose
    NE10_INV_BUTTERFLY_TMP( 0,0, 0,2,
                            0,0, 2,2);

    NE10_INV_BUTTERFLY_TMP( 1,2, 1,0,
                            0,1, 2,3);

    NE10_INV_BUTTERFLY_TMP( 0,1, 1,3,
                            1,0, 3,2);

    q_in3[3] *= - 1.0f;
    NE10_INV_BUTTERFLY_TMP( 1,1, 0,3,
                            3,3, 1,1);

    NE10_INV_BUTTERFLY_TMP( 2,0, 2,2,
                            0,2, 2,0);

    NE10_INV_BUTTERFLY_TMP( 3,2, 3,0,
                            0,3, 2,1);

    NE10_INV_BUTTERFLY_TMP( 2,1, 3,3,
                            1,2, 3,0);

    q_in3[1] *= - 1.0f;
    NE10_INV_BUTTERFLY_TMP( 3,1, 2,3,
                            3,1, 1,3);
#undef NE10_INV_BUTTERFLY_TMP

    // NE10_PRINT_Q_VECTOR(q_out0);
    // NE10_PRINT_Q_VECTOR(q_out1);
    // NE10_PRINT_Q_VECTOR(q_out2);
    // NE10_PRINT_Q_VECTOR(q_out3);

    // BUTTERFLY - radix 4x2
    // STAGE
    // q_out -> q_in

    // OUTPUT
    // INPUT
#define NE10_INV_BUTTERFLY_TMP(I1,I2,J1,J2,K1,K2,S1,S2) do {    \
    q_in ## I1 [I2] = ( q_out ## K1 [K2] + q_out ## S1 [S2] ); \
    q_in ## J1 [J2] = ( q_out ## K1 [K2] - q_out ## S1 [S2] ); \
} while(0);

    NE10_INV_BUTTERFLY_TMP(0,0, 0,2,
                           0,0, 0,1);

    NE10_INV_BUTTERFLY_TMP(1,0, 1,2,
                           1,0, 1,1);

    NE10_INV_BUTTERFLY_TMP(0,1, 0,3,
                           0,2, 0,3);

    NE10_INV_BUTTERFLY_TMP(1,1, 1,3,
                           1,2, 1,3);

    NE10_INV_BUTTERFLY_TMP(2,0, 2,2,
                           2,0, 2,1);

    NE10_INV_BUTTERFLY_TMP(3,0, 3,2,
                           3,0, 3,1);


    NE10_INV_BUTTERFLY_TMP(2,1, 2,3,
                           2,2, 2,3);

    NE10_INV_BUTTERFLY_TMP(3,1, 3,3,
                           3,2, 3,3);

    // NE10_PRINT_Q_VECTOR(q_in0);
    // NE10_PRINT_Q_VECTOR(q_in1);
    // NE10_PRINT_Q_VECTOR(q_in2);
    // NE10_PRINT_Q_VECTOR(q_in3);
#undef NE10_INV_BUTTERFLY_TMP

    // load tw
    q2_tw0[0][0] = tw[0];
    q2_tw0[0][1] = tw[2];
    q2_tw0[0][2] = tw[4];
    q2_tw0[0][3] = tw[6];
    q2_tw0[1][0] = tw[1];
    q2_tw0[1][1] = tw[3];
    q2_tw0[1][2] = tw[5];
    q2_tw0[1][3] = tw[7];

    q2_tw1[0][0] = tw[0+8];
    q2_tw1[0][1] = tw[2+8];
    q2_tw1[0][2] = tw[4+8];
    q2_tw1[0][3] = tw[6+8];
    q2_tw1[1][0] = tw[1+8];
    q2_tw1[1][1] = tw[3+8];
    q2_tw1[1][2] = tw[5+8];
    q2_tw1[1][3] = tw[7+8];

    // TW: in->out
    q_out0[0] = q_in0[0];
    q_out1[0] = q_in1[0];
    q_out2[0] = q_in2[0];
    q_out3[0] = q_in3[0];

    //----------------------------------------------------------//
    // first 2 lines
    //   R          R             R           I             I
    q_out0[1] = q_in0[1] * q2_tw0[0][1] + q_in1[1] * q2_tw0[1][1];
    //   I          R             I           I             R
    q_out1[1] = q_in0[1] * q2_tw0[1][1] - q_in1[1] * q2_tw0[0][1];

    //   R          R             R           I             I
    q_out0[2] = q_in0[2] * q2_tw0[0][2] + q_in1[2] * q2_tw0[1][2];
    //   I          R             I           I             R
    q_out1[2] = q_in0[2] * q2_tw0[1][2] - q_in1[2] * q2_tw0[0][2];

    //   R          R             R           I             I
    q_out0[3] = q_in0[3] * q2_tw0[0][3] + q_in1[3] * q2_tw0[1][3];
    //   I          R             I           I             R
    q_out1[3] = q_in0[3] * q2_tw0[1][3] - q_in1[3] * q2_tw0[0][3];

    //----------------------------------------------------------//
    // second 2 lines
    //   R          R             R           I             I
    q_out2[1] = q_in2[1] * q2_tw1[0][1] + q_in3[1] * q2_tw1[1][1];
    //   I          R             I           I             R
    q_out3[1] = q_in2[1] * q2_tw1[1][1] - q_in3[1] * q2_tw1[0][1];

    //   R          R             R           I             I
    q_out2[2] = q_in2[2] * q2_tw1[0][2] + q_in3[2] * q2_tw1[1][2];
    //   I          R             I           I             R
    q_out3[2] = q_in2[2] * q2_tw1[1][2] - q_in3[2] * q2_tw1[0][2];

    //   R          R             R           I             I
    q_out2[3] = q_in2[3] * q2_tw1[0][3] + q_in3[3] * q2_tw1[1][3];
    //   I          R             I           I             R
    q_out3[3] = q_in2[3] * q2_tw1[1][3] - q_in3[3] * q2_tw1[0][3];

    // STORE
    *(fout_r++) =   q_out0[0];
    *(fout_r++) =   q_out0[1];
    *(fout_r++) =   q_out0[2];
    *(fout_r++) =   q_out0[3];
    *(fout_r++) =   q_out1[0];
    *(fout_r++) = - q_out1[1];
    *(fout_r++) = - q_out1[2];
    *(fout_r++) = - q_out1[3];
    *(fout_r++) =   q_out2[0];
    *(fout_r++) =   q_out2[1];
    *(fout_r++) =   q_out2[2];
    *(fout_r++) =   q_out2[3];
    *(fout_r++) =   q_out3[0];
    *(fout_r++) = - q_out3[1];
    *(fout_r++) = - q_out3[2];
    *(fout_r++) = - q_out3[3];
}

NE10_INLINE void ne10_radix4_r2c_with_twiddles_last_stage_other_butterfly (ne10_fft_cpx_float32_t *dst,
                                            const ne10_fft_cpx_float32_t *src,
                                            const ne10_fft_cpx_float32_t *twiddles,
                                            const ne10_int32_t nfft)
{
    const ne10_float32_t *fin_r = ((const ne10_float32_t*) src) + 12 + 16;
    ne10_float32_t *fout_r = (ne10_float32_t*) dst + 8;
    ne10_float32_t *fout_b = (ne10_float32_t*) dst - 14;
    const ne10_float32_t *tw = ((const ne10_float32_t*) twiddles) + 8  + 16;

    // Take 4 elements as a set.
    // The leading 8 sets are already transformed in first and seconds butterflies.
    // This function transforms 8 sets in each loop.
    ne10_int32_t loop_count = ((nfft >> 2) - 8) >> 3;

	NE10_DECLARE_4 (CPLX, q2_in);	 // 8Q
	NE10_DECLARE_3 (CPLX, q2_tw);	 // 6Q
	NE10_DECLARE_4 (CPLX, q2_out);	 // 8Q

    for (; loop_count > 0; loop_count--)
    {

        /*  INPUT
         *  0R  1R  2R  3R      Q0
         *  0I  1I  2I  3I      Q1
         *  4R  5R  6R  7R      Q2
         *  4I  5I  6I  7I      Q3
         *  8R  9R  aR  bR      Q4
         *  8I  9I  aI  bI      Q5
         *  cR  dR  eR  fR      Q6
         *  cI  dI  eI  fI      Q7
         */

        // transpose
        // q2_out -> q2_in
        /*
         *      val[0]
         *  0R  4R  8R  cR      Q0
         *  1R  5R  9R  dR      Q2
         *  2R  6R  aR  eR      Q4
         *  3R  7R  bR  fR      Q6
         *
         *      val[1]
         *  0I  4I  8I  cI      Q1
         *  1I  5I  9I  dI      Q3
         *  2I  6I  aI  eI      Q5
         *  3I  7I  bI  fI      Q7
         */

		CPLX_LOAD(q2_out0,fin_r,0 * MXU_A_SIZE);
		CPLX_LOAD(q2_out1,fin_r,2 * MXU_A_SIZE);
		CPLX_LOAD(q2_out2,fin_r,4 * MXU_A_SIZE);
		CPLX_LOAD(q2_out3,fin_r,6 * MXU_A_SIZE);
		fin_r += 32;

		Radix4x4C_Transpose(q2_in, q2_out);
        // Load twiddles
		CPLX_LOAD(q2_tw0,tw,0 * MXU_A_SIZE);
		CPLX_LOAD(q2_tw1,tw,2 * MXU_A_SIZE);
		CPLX_LOAD(q2_tw2,tw,4 * MXU_A_SIZE);
		CPLX_TRANSPOSE(q2_tw0,0);
		CPLX_TRANSPOSE(q2_tw1,0);
		CPLX_TRANSPOSE(q2_tw2,0);
        tw += 24;

        // tw
        // q2_in -> q2_out
        q2_out0 = q2_in0;
        CPLX_MUL(q2_out1, q2_in1, q2_tw0);
		CPLX_MUL(q2_out2, q2_in2, q2_tw1);
		CPLX_MUL(q2_out3, q2_in3, q2_tw2);

        // butterfly
        // out -> in
		CPLX_ADD(q2_in0,q2_out0,q2_out2);
		CPLX_SUB(q2_in1,q2_out0,q2_out2);
		CPLX_ADD(q2_in2,q2_out1,q2_out3);
		CPLX_SUB(q2_in3,q2_out1,q2_out3);
        // in -> out

		CPLX_SUB(q2_out2,q2_in0,q2_in2);
		CPLX_INORDER_SUB(q2_out3,q2_in1,q2_in3);


        q2_out3.val[1] = -q2_out3.val[1];
        q2_out2.val[1] = -q2_out2.val[1];

		CPLX_ADD(q2_out0,q2_in0,q2_in2);
		CPLX_INORDER_ADD(q2_out1,q2_in1,q2_in3);

        // reverse -- CONJ
		NE10_REVERSE_V4F32(q2_out2.val[0]);
		NE10_REVERSE_V4F32(q2_out2.val[1]);

		NE10_REVERSE_V4F32(q2_out3.val[0]);
		NE10_REVERSE_V4F32(q2_out3.val[1]);


		CPLX_TRANSPOSE(q2_out0,1);
		CPLX_TRANSPOSE(q2_out1,1);
		CPLX_TRANSPOSE(q2_out2,1);
		CPLX_TRANSPOSE(q2_out3,1);

        // store
		CPLX_STORE(q2_out0,fout_r,0);
		CPLX_STOREX(q2_out1,fout_r,(nfft >> 1) * 4);
		CPLX_STOREX(q2_out3,fout_b,(nfft >> 1) * 4);
		CPLX_STOREX(q2_out2,fout_b,nfft * 4);


        fout_r += 8;
        fout_b -= 8;
    }
}

NE10_INLINE void ne10_radix4_c2r_with_twiddles_first_stage_other_butterfly (ne10_fft_cpx_float32_t *dst,
                                            const ne10_fft_cpx_float32_t *src,
                                            const ne10_fft_cpx_float32_t *twiddles,
                                            const ne10_int32_t nfft)
{
    ne10_float32_t *fout_r  =       ((ne10_float32_t*) dst ) + 12 + 16 ;
    const ne10_float32_t *fin_r =    (const ne10_float32_t*) src + 8;
    const ne10_float32_t *fin_b =    (const ne10_float32_t*) src - 14;
    const ne10_float32_t *tw     = ((const ne10_float32_t*) twiddles) + 8  + 16;
    ne10_int32_t loop_count = ((nfft>>2)-8)>>3;

	NE10_DECLARE_4(CPLX,q2_in);    // 8Q
	NE10_DECLARE_3(CPLX,q2_tw);    // 6Q
	NE10_DECLARE_4(CPLX,q2_out);   // 8Q

    for ( ; loop_count>0; loop_count -- )
    {

        /*  INPUT
         *  0R  1R  2R  3R      Q0
         *  0I  1I  2I  3I      Q1
         *  4R  5R  6R  7R      Q2
         *  4I  5I  6I  7I      Q3
         *  8R  9R  aR  bR      Q4
         *  8I  9I  aI  bI      Q5
         *  cR  dR  eR  fR      Q6
         *  cI  dI  eI  fI      Q7
         */

		CPLX_LOAD(q2_in0,fin_r,0);
        CPLX_LOADX(q2_in1,fin_r,(nfft>>1) * 4);
        fin_r += 8;

        CPLX_LOADX(q2_in3,fin_b , (nfft>>1) * 4);
        CPLX_LOADX(q2_in2,fin_b , nfft * 4);
        fin_b -= 8;

        CPLX_LOAD(q2_tw0,tw,0 );
        CPLX_LOAD(q2_tw1,tw,2 * MXU_A_SIZE);
        CPLX_LOAD(q2_tw2,tw,4 * MXU_A_SIZE);

		CPLX_TRANSPOSE(q2_in0,0);
		CPLX_TRANSPOSE(q2_in1,0);
		CPLX_TRANSPOSE(q2_in2,0);
		CPLX_TRANSPOSE(q2_in3,0);

		CPLX_TRANSPOSE(q2_tw0,0);
		CPLX_TRANSPOSE(q2_tw1,0);
		CPLX_TRANSPOSE(q2_tw2,0);

        tw += 8 * 3;


        // reverse -- CONJ
        NE10_REVERSE_V4F32(q2_in3.val[0]);
		NE10_REVERSE_V4F32(q2_in3.val[1]);
		NE10_REVERSE_V4F32(q2_in2.val[0]);
		NE10_REVERSE_V4F32(q2_in2.val[1]);


        q2_in2.val[1] = -q2_in2.val[1];
        q2_in3.val[1] = -q2_in3.val[1];

        // in -> out
        q2_out0.val[0] = q2_in0.val[0] + q2_in2.val[0];
		q2_out0.val[1] = q2_in0.val[1] + q2_in2.val[1];

		q2_out2.val[0] = q2_in0.val[0] - q2_in2.val[0];
		q2_out2.val[1] = q2_in0.val[1] - q2_in2.val[1];

		q2_out1.val[0] = q2_in1.val[0] + q2_in3.val[0];
		q2_out3.val[1] = q2_in1.val[0] - q2_in3.val[0];

		q2_out1.val[1] = q2_in3.val[1] + q2_in1.val[1];
		q2_out3.val[0] = q2_in3.val[1] - q2_in1.val[1];

		// out -> in
		q2_in0.val[0] = q2_out0.val[0] + q2_out1.val[0];
		q2_in2.val[0] = q2_out0.val[0] - q2_out1.val[0];

		q2_in0.val[1] = q2_out0.val[1] + q2_out1.val[1];
		q2_in2.val[1] = q2_out0.val[1] - q2_out1.val[1];

		q2_in1.val[0] = q2_out2.val[0] + q2_out3.val[0];
		q2_in3.val[0] = q2_out2.val[0] - q2_out3.val[0];

		q2_in1.val[1] = q2_out2.val[1] + q2_out3.val[1];
		q2_in3.val[1] = q2_out2.val[1] - q2_out3.val[1];

        // tw
        // q2_in -> q2_out
        q2_out0 = q2_in0;

        CPLX_INV_MUL(q2_out1,q2_in1,q2_tw0);
		CPLX_INV_MUL(q2_out2,q2_in2,q2_tw1);
		CPLX_INV_MUL(q2_out3,q2_in3,q2_tw2);

        // transpose
        // q2_out -> q2_in
		Radix4x4C_Transpose(q2_in, q2_out);

        // store
		CPLX_STORE(q2_in0,fout_r,0 * MXU_A_SIZE);
		CPLX_STORE(q2_in1,fout_r,2 * MXU_A_SIZE);
		CPLX_STORE(q2_in2,fout_r,4 * MXU_A_SIZE);
		CPLX_STORE(q2_in3,fout_r,6 * MXU_A_SIZE);
        fout_r += 32;
    }
}

NE10_INLINE void ne10_radix4_r2c_with_twiddles_last_stage( ne10_fft_cpx_float32_t *dst,
                                            const ne10_fft_cpx_float32_t *src,
                                            const ne10_fft_cpx_float32_t *twiddles,
                                            const ne10_int32_t nfft)
{
    ne10_radix4_r2c_with_twiddles_last_stage_first_butterfly(dst,src,twiddles,nfft);

    if (nfft==16)
    {
        return;
    }

    ne10_radix4_r2c_with_twiddles_last_stage_second_butterfly(dst,src,twiddles,nfft);

    if (nfft==32)
    {
        return;
    }

    ne10_radix4_r2c_with_twiddles_last_stage_other_butterfly(dst,src,twiddles,nfft);
}

NE10_INLINE void ne10_radix4_c2r_with_twiddles_first_stage( ne10_fft_cpx_float32_t *dst,
                                            const ne10_fft_cpx_float32_t *src,
                                            const ne10_fft_cpx_float32_t *twiddles,
                                            const ne10_int32_t nfft)
{
    ne10_radix4_c2r_with_twiddles_first_stage_first_butterfly(dst,src,twiddles,nfft);

    if (nfft==16)
    {
        return;
    }

    ne10_radix4_c2r_with_twiddles_first_stage_second_butterfly(dst,src,twiddles,nfft);

    if (nfft==32)
    {
        return;
    }

    ne10_radix4_c2r_with_twiddles_first_stage_other_butterfly(dst,src,twiddles,nfft);
}

/**
 * @ingroup R2C_FFT_IFFT
 * Specific implementation of @ref ne10_fft_r2c_1d_float32 using MXU SIMD capabilities.
 */
void ne10_fft_r2c_1d_float32_mxu (ne10_fft_cpx_float32_t *fout,
                                   ne10_float32_t *fin,
                                   ne10_fft_r2c_cfg_float32_t cfg)
{
    typedef         ne10_float32_t REAL;
    typedef ne10_fft_cpx_float32_t CPLX;

    ne10_fft_cpx_float32_t * tmpbuf = cfg->buffer;
    ne10_float32_t *fout_r = (ne10_float32_t*) fout;

    (void)sizeof(REAL);
    (void)fout_r;

	ne10_mixed_radix_r2c_butterfly_float32_mxu (fout, (CPLX*) fin, cfg->r_factors_neon, cfg->r_twiddles_neon, tmpbuf);


	ne10_radix4_r2c_with_twiddles_last_stage(fout, tmpbuf, cfg->r_super_twiddles_neon, cfg->nfft);

	if(cfg->r_factors_neon[NE10_MAXFACTORS * 2 - 1] == 1){
		/* fout[cfg->nfft / 2].r = fout[0].i; */
		fout[cfg->nfft / 2].r = fout[0].i;
		fout[cfg->nfft / 2].i = 0.0f;
	}
	else
	{
		fout[cfg->nfft / 2].r = fout[0].i;
		fout[0].i = fout[cfg->nfft / 2].i = 0.0f;
	}
}

/**
 * @ingroup R2C_FFT_IFFT
 * Specific implementation of @ref ne10_fft_c2r_1d_float32 using MXU SIMD capabilities.
 */
void ne10_fft_c2r_1d_float32_mxu (ne10_float32_t *fout,
                                   ne10_fft_cpx_float32_t *fin,
                                   ne10_fft_r2c_cfg_float32_t cfg)
{
    typedef         ne10_float32_t REAL;
    typedef ne10_fft_cpx_float32_t CPLX;

    (void)sizeof(REAL);

    ne10_fft_cpx_float32_t * tmpbuf = cfg->buffer;
    ne10_fft_cpx_float32_t * fout_c;
    ne10_int32_t stage_count;
    ne10_int32_t radix;
	stage_count = cfg->r_factors_neon[0];
	radix       = cfg->r_factors_neon[  stage_count << 1 ];
	if (radix==2)
	{
		stage_count --;
	}
	if(cfg->r_factors_neon[NE10_MAXFACTORS * 2 - 1] == 0)
		fin[0].i = fin[cfg->nfft>>1].r;
	fout_c = (stage_count % 2==1) ? tmpbuf : (CPLX*)fout;
	ne10_radix4_c2r_with_twiddles_first_stage( (CPLX*) fout_c, fin, cfg->r_super_twiddles_neon, cfg->nfft);
	ne10_mixed_radix_c2r_butterfly_float32_mxu ( (CPLX*) fout, (CPLX*) NULL, cfg->r_factors_neon, cfg->r_twiddles_neon_backward, tmpbuf);
	if(cfg->r_factors_neon[NE10_MAXFACTORS * 2 - 1] == 0)
		fin[0].i = 0.0f;
}

void ReverseForward(ne10_float32_t *out,ne10_float32_t *in,int len){
	int i;
	out[0] = in[0];
	out[len / 2] = in[1];
	for(i = 1;i < len / 2;i++){
		out[i] = in[i * 2];
		out[len - i] = -in[i * 2 + 1];
	}
}

void ReverseBackword(ne10_float32_t *out,ne10_float32_t *in,int len){
	int i;

	out[0] = in[0];
	out[1] = in[len / 2];
	for(i = 1;i < len / 2;i++){
		out[i * 2] = in[i];
		out[i * 2 + 1] = -in[len - i];
	}
}

NE10_INLINE void ne10_mixed_radix_r2c_butterfly_float32_mxu_A (ne10_fft_cpx_float32_t * Fin,
															   const ne10_int32_t * factors,
															   const ne10_fft_cpx_float32_t * twiddles,
															   ne10_fft_cpx_float32_t * buffer)
{
    ne10_int32_t fstride, mstride, nfft,prescaling;
    ne10_int32_t radix;
    ne10_int32_t stage_count;

    // PRINT_STAGE_INFO;

    // init fstride, mstride, radix, nfft
    stage_count = factors[0];
    fstride     = factors[1];
    mstride     = factors[ (stage_count << 1) - 1 ];
    radix       = factors[  stage_count << 1 ];
    nfft        = radix * fstride; // not the real nfft
	prescaling = factors[NE10_MAXFACTORS * 2 - 1];

    // the first stage
    if (radix == 8)   // length of FFT is 2^n (n is odd)
    {
        ne10_radix8x4_r2c_mxu (buffer, Fin, fstride, mstride, nfft, prescaling);
    }
    else if (radix == 4)   // length of FFT is 2^n (n is even)
    {
        ne10_radix4x4_r2c_mxu (buffer, Fin, fstride, mstride, nfft, prescaling);
    }
    // end of first stage

    // others
    for (; fstride > 1;)
    {
        fstride >>= 2;
        ne10_radix4x4_r2c_with_twiddles_mxu (Fin, buffer, fstride, mstride, nfft, twiddles);
        twiddles += 3 * mstride;
        mstride <<= 2;
        ne10_swap_ptr (buffer, Fin);
    } // other stage

}


/**
 * @ingroup R2C_FFT_IFFT
 * Specific implementation of @ref ne10_fft_r2c_1d_float32 using MXU SIMD capabilities.
 */
void ne10_fft_r2c_1d_float32_mxu_A (ne10_float32_t *fin,
									ne10_fft_r2c_cfg_float32_t cfg)
{
    ne10_fft_cpx_float32_t * tmpbuf = cfg->buffer;
	ne10_mixed_radix_r2c_butterfly_float32_mxu_A ((ne10_fft_cpx_float32_t*) fin, cfg->r_factors_neon, cfg->r_twiddles_neon, tmpbuf);
	int stage_count = cfg->r_factors_neon[0];
	if((stage_count % 2)  == 0)
		ne10_swap_ptr (tmpbuf, fin);

	ne10_radix4_r2c_with_twiddles_last_stage((ne10_fft_cpx_float32_t *)fin, tmpbuf, cfg->r_super_twiddles_neon, cfg->nfft);
	ReverseForward((ne10_float32_t*)tmpbuf,fin,cfg->nfft);
	if((stage_count % 2)  == 1){
		memcpy(fin,tmpbuf,cfg->nfft * 4);
	}

}

NE10_INLINE void ne10_radix8x4_c2r_mxu_A (ne10_fft_cpx_float32_t *Fout,
										  const ne10_fft_cpx_float32_t *Fin,
										  const ne10_int32_t fstride,
										  const ne10_int32_t mstride,
										  const ne10_int32_t nfft,
										  ne10_int32_t prescaling)
{
    ne10_int32_t f_count;

    NE10_DECLARE_8(v4f32,q_in);
    NE10_DECLARE_8(v4f32,q_out);

    const v4f32 *Fin_mxu  = (v4f32*) Fin;
	v4f32 *Fout_mxu = (v4f32*) Fout;

    for (f_count = fstride; f_count > 0; f_count --)
    {
        // from Fin_mxu load 8 v4f32 into q_in0 ~ q_in7, by step = 1
		NE10_RADIX8x4_R2C_MXU_LOAD(Fin_mxu,q_in,1);
		Fin_mxu += 8;


        // NE10_PRINT_Qx8_VECTOR(q_in);

        NE10_RADIX8x4_C2R_MXU_KERNEL(q_out,q_in);

        // NE10_PRINT_Qx8_VECTOR(q_out);

	// store
        NE10_RADIX8x4_R2C_MXU_STOREX(Fout_mxu,q_out,fstride);

        Fout_mxu ++;
    }
}

NE10_INLINE void ne10_mixed_radix_r2c_butterfly_float32_mxu_B (ne10_fft_cpx_float32_t * Fin,
															   const ne10_int32_t * factors,
															   const ne10_fft_cpx_float32_t * twiddles,
															   ne10_fft_cpx_float32_t * buffer)
{
    ne10_int32_t fstride, mstride, nfft,prescaling;
    ne10_int32_t radix;
    ne10_int32_t stage_count;

    // PRINT_STAGE_INFO;

    // init fstride, mstride, radix, nfft
    stage_count = factors[0];
    fstride     = factors[1];
    mstride     = factors[ (stage_count << 1) - 1 ];
    radix       = factors[  stage_count << 1 ];
    nfft        = radix * fstride; // not the real nfft
	prescaling = factors[NE10_MAXFACTORS * 2 - 1];

    // the first stage
    if (radix == 8)   // length of FFT is 2^n (n is odd)
    {
        ne10_radix8x4_r2c_mxu (buffer, Fin, fstride, mstride, nfft, prescaling);
    }
    else if (radix == 4)   // length of FFT is 2^n (n is even)
    {
        ne10_radix4x4_r2c_mxu (buffer, Fin, fstride, mstride, nfft, prescaling);
    }
    // end of first stage

    // others
    for (; fstride > 1;)
    {
        fstride >>= 2;
        ne10_radix4x4_r2c_with_twiddles_mxu (Fin, buffer, fstride, mstride, nfft, twiddles);
        twiddles += 3 * mstride;
        mstride <<= 2;
        ne10_swap_ptr (buffer, Fin);
    } // other stage

}

NE10_INLINE void ne10_radix8x4_c2r_mxu_B (ne10_fft_cpx_float32_t *Fout,
										  const ne10_fft_cpx_float32_t *Fin,
										  const ne10_int32_t fstride,
										  const ne10_int32_t mstride,
										  const ne10_int32_t nfft,
										  ne10_int32_t prescaling)
{
    ne10_int32_t f_count;

    NE10_DECLARE_8(v4f32,q_in);
    NE10_DECLARE_8(v4f32,q_out);

    ne10_float32_t one_by_N = 0.25 / nfft;

    if(prescaling == 1)
	    one_by_N = 0.5;
    else
	    one_by_N = 0.25 / nfft;

    v4f32 one_by_N_mxu = FILL_F(one_by_N);


    const v4f32 *Fin_mxu  = (v4f32*) Fin;
	v4f32 *Fout_mxu = (v4f32*) Fout;

    for (f_count = fstride; f_count > 0; f_count --)
    {
        // from Fin_mxu load 8 v4f32 into q_in0 ~ q_in7, by step = 1
		NE10_RADIX8x4_R2C_MXU_LOAD(Fin_mxu,q_in,1);
		Fin_mxu += 8;


        // NE10_PRINT_Qx8_VECTOR(q_in);

        NE10_RADIX8x4_C2R_MXU_KERNEL(q_out,q_in);

        // NE10_PRINT_Qx8_VECTOR(q_out);

#ifdef NE10_DSP_RFFT_SCALING
		NE10_RADIX8x4_C2R_MXU_KERNEL_SCALE_DATA(q_out,one_by_N_mxu);
#endif
	// store
        NE10_RADIX8x4_R2C_MXU_STOREX(Fout_mxu,q_out,fstride);

        Fout_mxu ++;
    }
}

NE10_INLINE void ne10_radix4x4_c2r_mxu_A (ne10_fft_cpx_float32_t *Fout,
										  const ne10_fft_cpx_float32_t *Fin,
										  const ne10_int32_t fstride,
										  const ne10_int32_t mstride,
										  const ne10_int32_t nfft,
										  ne10_int32_t prescaling)
{
	ne10_int32_t f_count;
	const v4f32 *Fin_mxu  = (v4f32*) Fin;
	v4f32 *Fout_mxu = (v4f32*) Fout;

	for (f_count = 0; f_count < fstride; f_count ++)
	{
		NE10_DECLARE_4(v4f32,q_in);
		NE10_DECLARE_4(v4f32,q_out);

		// load
		NE10_RADIX4x4_R2C_MXU_LOAD(Fin_mxu,q_in,1);
		Fin_mxu += 4;

        // NE10_PRINT_Qx4_VECTOR(q_in);

        NE10_RADIX4x4_C2R_MXU_KERNEL(q_out,q_in);

        // NE10_PRINT_Qx4_VECTOR(q_out);

	// store
        NE10_RADIX4x4_R2C_MXU_STOREX(Fout_mxu,q_out,fstride);
        Fout_mxu ++;
    }
}

NE10_INLINE void ne10_radix4x4_c2r_mxu_B (ne10_fft_cpx_float32_t *Fout,
										  const ne10_fft_cpx_float32_t *Fin,
										  const ne10_int32_t fstride,
										  const ne10_int32_t mstride,
										  const ne10_int32_t nfft,
										  ne10_int32_t prescaling)
{
	ne10_int32_t f_count;
	const v4f32 *Fin_mxu  = (v4f32*) Fin;
	v4f32 *Fout_mxu = (v4f32*) Fout;

	ne10_float32_t one_by_N;
	v4f32 one_by_N_mxu;
	if(prescaling == 1)
	{
		one_by_N = 0.5;
	}else
		one_by_N = 0.25 / nfft;
	one_by_N_mxu = FILL_F(one_by_N);

	for (f_count = 0; f_count < fstride; f_count ++)
	{
		NE10_DECLARE_4(v4f32,q_in);
		NE10_DECLARE_4(v4f32,q_out);

		// load
		NE10_RADIX4x4_R2C_MXU_LOAD(Fin_mxu,q_in,1);
		Fin_mxu += 4;

		// NE10_PRINT_Qx4_VECTOR(q_in);

		NE10_RADIX4x4_C2R_MXU_KERNEL(q_out,q_in);

		// NE10_PRINT_Qx4_VECTOR(q_out);

#ifdef NE10_DSP_RFFT_SCALING
		NE10_RADIX4x4_C2R_MXU_KERNEL_SCALE_DATA(q_out,one_by_N_mxu);
#endif

		// store
		NE10_RADIX4x4_R2C_MXU_STOREX(Fout_mxu,q_out,fstride);
		Fout_mxu ++;
	}
}

NE10_INLINE void ne10_mixed_radix_c2r_butterfly_float32_mxu_A (ne10_fft_cpx_float32_t * Fout,
								const ne10_fft_cpx_float32_t * Fin,
								const ne10_int32_t * factors,
								const ne10_fft_cpx_float32_t * twiddles,
								ne10_fft_cpx_float32_t * buffer)
{
    ne10_int32_t fstride, mstride, nfft, prescaling;
    ne10_int32_t radix;
    ne10_int32_t stage_count;

    // PRINT_STAGE_INFO;

    // init fstride, mstride, radix, nfft
    stage_count = factors[0];
    fstride     = factors[1];

    mstride     = factors[ (stage_count << 1) - 1 ];
    radix       = factors[  stage_count << 1 ];
    nfft        = radix * fstride; // not the real nfft
	prescaling = factors[NE10_MAXFACTORS * 2 - 1];
    // fstride, mstride for last last stage
    fstride = 1;
    mstride = nfft >> 2;

    // others but the first stage
    for (; stage_count > 1;)
    {
        twiddles -= 3 * mstride;

        ne10_radix4x4_c2r_with_twiddles_mxu (Fout, buffer, fstride, mstride, nfft, twiddles);

        fstride <<= 2;
        mstride >>= 2;
        stage_count --;
        ne10_swap_ptr (buffer, Fout);
    }
    // first stage -- inversed
    if (radix == 8)   // length of FFT is 2^n (n is odd)
    {
        ne10_radix8x4_c2r_mxu_A (Fout, buffer, fstride, mstride, nfft, 0);
    }
    else if (radix == 4)   // length of FFT is 2^n (n is even)
    {
        ne10_radix4x4_c2r_mxu_A (Fout, buffer, fstride, mstride, nfft, 0);
    }

    (void)prescaling;
}


NE10_INLINE void ne10_mixed_radix_c2r_butterfly_float32_mxu_B (ne10_fft_cpx_float32_t * Fout,
								const ne10_fft_cpx_float32_t * Fin,
								const ne10_int32_t * factors,
								const ne10_fft_cpx_float32_t * twiddles,
								ne10_fft_cpx_float32_t * buffer)
{
    ne10_int32_t fstride, mstride, nfft, prescaling;
    ne10_int32_t radix;
    ne10_int32_t stage_count;

    // PRINT_STAGE_INFO;

    // init fstride, mstride, radix, nfft
    stage_count = factors[0];
    fstride     = factors[1];

    mstride     = factors[ (stage_count << 1) - 1 ];
    radix       = factors[  stage_count << 1 ];
    nfft        = radix * fstride; // not the real nfft
	prescaling = factors[NE10_MAXFACTORS * 2 - 1];
    // fstride, mstride for last last stage
    fstride = 1;
    mstride = nfft >> 2;

    // others but the first stage
    for (; stage_count > 1;)
    {
        twiddles -= 3 * mstride;

        ne10_radix4x4_c2r_with_twiddles_mxu (Fout, buffer, fstride, mstride, nfft, twiddles);

        fstride <<= 2;
        mstride >>= 2;
        stage_count --;
        ne10_swap_ptr (buffer, Fout);
    }
    // first stage -- inversed
    if (radix == 8)   // length of FFT is 2^n (n is odd)
    {
        ne10_radix8x4_c2r_mxu_B (Fout, buffer, fstride, mstride, nfft, 0);
    }
    else if (radix == 4)   // length of FFT is 2^n (n is even)
    {
        ne10_radix4x4_c2r_mxu_B (Fout, buffer, fstride, mstride, nfft, 0);
    }

    (void)prescaling;
}



/**
 * @ingroup R2C_FFT_IFFT
 * Specific implementation of @ref ne10_fft_c2r_1d_float32 using MXU SIMD capabilities.
 */
void ne10_fft_c2r_1d_float32_mxu_A (ne10_float32_t *fin,
									ne10_fft_r2c_cfg_float32_t cfg)
{
    typedef         ne10_float32_t REAL;
    typedef ne10_fft_cpx_float32_t CPLX;

    ne10_fft_cpx_float32_t * tmpbuf = cfg->buffer;
    ne10_fft_cpx_float32_t * fout_c;
    ne10_int32_t stage_count;
    ne10_int32_t radix;

    (void)sizeof(REAL);
    (void)fout_c;

	stage_count = cfg->r_factors_neon[0];
	radix       = cfg->r_factors_neon[  stage_count << 1 ];
	if (radix==2)
	{
		stage_count --;
	}
	ReverseBackword((ne10_float32_t *)tmpbuf,(ne10_float32_t *)fin,cfg->nfft);

	ne10_radix4_c2r_with_twiddles_first_stage( (CPLX*) fin, tmpbuf, cfg->r_super_twiddles_neon, cfg->nfft);
	ne10_mixed_radix_c2r_butterfly_float32_mxu_A ( (CPLX*) tmpbuf, (CPLX*) NULL, cfg->r_factors_neon, cfg->r_twiddles_neon_backward, (ne10_fft_cpx_float32_t *)fin);

    if (stage_count % 2 == 1)
    {
        memcpy(fin,tmpbuf,cfg->nfft * 4);
    }
}

//-------------------------------------------------------------------


/**
 * @ingroup R2C_FFT_IFFT
 * Specific implementation of @ref ne10_fft_r2c_1d_float32 using MXU SIMD capabilities.
 */

void ne10_fft_r2c_1d_float32_mxu_B (ne10_float32_t *fin, ne10_fft_r2c_cfg_float32_t cfg)
{
    ne10_fft_cpx_float32_t * tmpbuf = cfg->buffer;
	ne10_mixed_radix_r2c_butterfly_float32_mxu_B ((ne10_fft_cpx_float32_t*) fin, cfg->r_factors_neon, cfg->r_twiddles_neon, tmpbuf);
	int stage_count = cfg->r_factors_neon[0];
	if((stage_count % 2)  == 0){
		ne10_swap_ptr (tmpbuf, fin);
	}
	ne10_radix4_r2c_with_twiddles_last_stage((ne10_fft_cpx_float32_t *)fin, tmpbuf, cfg->r_super_twiddles_neon, cfg->nfft);

	if((stage_count % 2)  == 0){
		memcpy(tmpbuf,fin,cfg->nfft * 4);
	}

}

void ne10_fft_c2r_1d_float32_mxu_B (ne10_float32_t *fin, ne10_fft_r2c_cfg_float32_t cfg)
{
	typedef         ne10_float32_t REAL;
	typedef ne10_fft_cpx_float32_t CPLX;

	ne10_fft_cpx_float32_t * tmpbuf = cfg->buffer;
	ne10_fft_cpx_float32_t * fout_c;
	ne10_int32_t stage_count;
	ne10_int32_t radix;

    (void)sizeof(REAL);
    (void)fout_c;

	stage_count = cfg->r_factors_neon[0];
	radix       = cfg->r_factors_neon[  stage_count << 1 ];
	if (radix==2)
	{
		stage_count --;
	}
	ne10_radix4_c2r_with_twiddles_first_stage( (CPLX*) tmpbuf, (CPLX*)fin, cfg->r_super_twiddles_neon, cfg->nfft);
	ne10_mixed_radix_c2r_butterfly_float32_mxu_B ( (CPLX*) fin, (CPLX*) NULL, cfg->r_factors_neon, cfg->r_twiddles_neon_backward, tmpbuf);

	if (stage_count % 2 == 0)
	{
		memcpy(fin,tmpbuf,cfg->nfft * 4);
	}
}

void ne10_realfft(ne10_float32_t *fin, ne10_fft_r2c_cfg_float32_t cfg)
{
	ne10_int32_t i = 2;
	ne10_float32_t tmp;
	ne10_fft_r2c_1d_float32_mxu_B (fin, cfg);
	tmp = fin[1];
	for(i = 2;i < (cfg->nfft - 2) / 8 * 8; i += 8){
		v4f32 vsrc[2];
		vsrc[0] = LOAD_F(&fin[i], 0 * MXU_A_SIZE);
		vsrc[1] = LOAD_F(&fin[i], 1 * MXU_A_SIZE);
		STORE_F(vsrc[0], &fin[i-1], 0 * MXU_A_SIZE);
		STORE_F(vsrc[1], &fin[i-1], 1 * MXU_A_SIZE);
	}
	for(;i < cfg->nfft;i++)
		fin[i - 1] = fin[i];
	fin[cfg->nfft - 1] = tmp;
}

void ne10_realifft(ne10_float32_t *fin, ne10_fft_r2c_cfg_float32_t cfg)
{
	ne10_float32_t tmp;
	int i = cfg->nfft - 1;
	int end = cfg->nfft - (cfg->nfft - 2) / 8 * 8;
	tmp = fin[cfg->nfft - 1];

	for(i = cfg->nfft - 1;i >= end;i-=8){
		v4f32 vsrc[2];
		vsrc[0] = LOAD_F(&fin[i - 8], 0 * MXU_A_SIZE);
		vsrc[1] = LOAD_F(&fin[i - 8], 1 * MXU_A_SIZE);
		STORE_F(vsrc[0], &fin[i - 7], 0 * MXU_A_SIZE);
		STORE_F(vsrc[1], &fin[i - 7], 1 * MXU_A_SIZE);
	}

	for(;i >= 2;i--)
		fin[i] = fin[i - 1];
	fin[1] = tmp;
	ne10_fft_c2r_1d_float32_mxu_B(fin, cfg);
}



/******************************************************************************/
/**
 * @brief 基于R2C FFT实现C2C FFT（正变换）MXU优化接口
 * @param fout 输出复序列（长度nfft，存储格式：{r0,i0,r1,i1,...rn-1,in-1}）
 * @param fin  输入复序列（长度nfft，存储格式：{r0,i0,r1,i1,...rn-1,in-1}）
 * @param cfg  R2C FFT配置（需提前初始化，nfft为复序列长度）
 */
#define MAX_C2C_NFFT 512
static ne10_float32_t _c2c_a[MAX_C2C_NFFT];
static ne10_float32_t _c2c_b[MAX_C2C_NFFT];
static ne10_fft_cpx_float32_t _c2c_A_cpx[MAX_C2C_NFFT/2 + 1];
static ne10_fft_cpx_float32_t _c2c_B_cpx[MAX_C2C_NFFT/2 + 1];

void ne10_fft_c2c_1d_float32_mxu_ai (ne10_fft_cpx_float32_t *fout,
                                   ne10_fft_cpx_float32_t *fin,
                                   ne10_fft_r2c_cfg_float32_t cfg)
{
    ne10_int32_t nfft = cfg->nfft;
    ne10_int32_t half_nfft = nfft / 2;
    
    ne10_float32_t *a = _c2c_a;
    ne10_float32_t *b = _c2c_b;
    ne10_fft_cpx_float32_t *A_cpx = _c2c_A_cpx;
    ne10_fft_cpx_float32_t *B_cpx = _c2c_B_cpx;
    
    ne10_int32_t k;

    // 步骤1：拆分复序列为实部a[n]和虚部b[n]
    for (k = 0; k < nfft; k++)
    {
        a[k] = fin[k].r;
        b[k] = fin[k].i;
    }

    // 步骤2：对实部a[n]和虚部b[n]分别执行R2C FFT（正变换）
    ne10_fft_r2c_1d_float32_mxu (A_cpx, a, cfg);
    ne10_fft_r2c_1d_float32_mxu (B_cpx, b, cfg);

    // 步骤3：利用共轭对称性补全实部完整频域A[k]和虚部完整频域B[k]，并合并为C2C结果Z[k]
    // 处理0 ~ nfft/2点
    for (k = 0; k <= half_nfft; k++)
    {
        // Z[k] = A[k] + jB[k]
        // 实部：Re(Z[k]) = Re(A[k]) - Im(B[k])
        // 虚部：Im(Z[k]) = Im(A[k]) + Re(B[k])
        fout[k].r = A_cpx[k].r - B_cpx[k].i;
        fout[k].i = A_cpx[k].i + B_cpx[k].r;
    }

    // 处理nfft/2+1 ~ nfft-1点（共轭补全）
    for (k = 1; k < half_nfft; k++)
    {
        ne10_int32_t sym_k = nfft - k;
        // A[sym_k] = conj(A[k])
        ne10_float32_t A_sym_r = A_cpx[k].r;
        ne10_float32_t A_sym_i = -A_cpx[k].i;
        // B[sym_k] = conj(B[k])
        ne10_float32_t B_sym_r = B_cpx[k].r;
        ne10_float32_t B_sym_i = -B_cpx[k].i;

        // Z[sym_k] = A[sym_k] + jB[sym_k]
        fout[sym_k].r = A_sym_r - B_sym_i;
        fout[sym_k].i = A_sym_i + B_sym_r;
    }

    // 释放临时内存 (已改用静态数组，无需释放)
}

/**
 * @brief 基于C2R IFFT实现C2C IFFT（逆变换）MXU优化接口
 * @param fout 输出复序列（长度nfft，存储格式：{r0,i0,r1,i1,...rn-1,in-1}）
 * @param fin  输入复序列（长度nfft，存储格式：{r0,i0,r1,i1,...rn-1,in-1}）
 * @param cfg  R2C FFT配置（需提前初始化，nfft为复序列长度）
 * @note 逆变换结果已做归一化（除以nfft）
 */
static ne10_float32_t _c2c_inv_A[MAX_C2C_NFFT];
static ne10_float32_t _c2c_inv_B[MAX_C2C_NFFT];
static ne10_fft_cpx_float32_t _c2c_inv_a_cpx[MAX_C2C_NFFT/2 + 1];
static ne10_fft_cpx_float32_t _c2c_inv_b_cpx[MAX_C2C_NFFT/2 + 1];

void ne10_fft_c2c_1d_inverse_float32_mxu_ai (ne10_fft_cpx_float32_t *fout,
                                           ne10_fft_cpx_float32_t *fin,
                                           ne10_fft_r2c_cfg_float32_t cfg)
{
    ne10_int32_t nfft = cfg->nfft;
    ne10_int32_t half_nfft = nfft / 2;
    
    ne10_float32_t *A = _c2c_inv_A;
    ne10_float32_t *B = _c2c_inv_B;
    ne10_fft_cpx_float32_t *a_cpx = _c2c_inv_a_cpx;
    ne10_fft_cpx_float32_t *b_cpx = _c2c_inv_b_cpx;
    
    ne10_int32_t k;

    // 步骤1：拆分C2C频域序列为实部分量A[k]和虚部分量B[k]
    // Z[k] = A[k] + jB[k] → 逆变换：z[n] = IFFT{A[k]} + jIFFT{B[k]}
    for (k = 0; k < nfft; k++)
    {
        // 频域拆分：A[k] = Re(Z[k]), B[k] = Im(Z[k])
        // 逆变换前需对B[k]取负（满足逆变换共轭特性）
        A[k] = fin[k].r;
        B[k] = -fin[k].i;
    }

    // 步骤2：补全A[k]和B[k]的精简频域格式（适配C2R IFFT输入）
    // 0 ~ nfft/2点直接复制
    for (k = 0; k <= half_nfft; k++)
    {
        a_cpx[k].r = A[k];
        a_cpx[k].i = (k == 0 || k == half_nfft) ? 0.0f : A[nfft - k]; // 共轭对称性
        b_cpx[k].r = B[k];
        b_cpx[k].i = (k == 0 || k == half_nfft) ? 0.0f : B[nfft - k]; // 共轭对称性
    }

    // 步骤3：对A[k]和B[k]分别执行C2R IFFT（逆变换），得到时域实部a[n]和虚部b[n]
    ne10_fft_c2r_1d_float32_mxu ((ne10_float32_t*)a_cpx, a_cpx, cfg);
    ne10_fft_c2r_1d_float32_mxu ((ne10_float32_t*)b_cpx, b_cpx, cfg);

    // 步骤4：合并时域实部和虚部，得到C2C IFFT结果
    for (k = 0; k < nfft; k++)
    {
        fout[k].r = ((ne10_float32_t*)a_cpx)[k];
        fout[k].i = ((ne10_float32_t*)b_cpx)[k];
    }

    // 释放临时内存 (已改用静态数组，无需释放)
}

/**
 * @brief 简化接口：直接操作浮点数组的C2C FFT（输入输出均为float数组，格式：r0,i0,r1,i1...）
 */
void ne10_fft_c2c_1d_float32_array_mxu (float *fout, float *fin, ne10_fft_r2c_cfg_float32_t cfg)
{
    ne10_int32_t nfft = cfg->nfft;

    (void)nfft;

    ne10_fft_cpx_float32_t *fin_cpx = (ne10_fft_cpx_float32_t*)fin;
    ne10_fft_cpx_float32_t *fout_cpx = (ne10_fft_cpx_float32_t*)fout;
    ne10_fft_c2c_1d_float32_mxu_ai (fout_cpx, fin_cpx, cfg);
}

/**
 * @brief 简化接口：直接操作浮点数组的C2C IFFT（输入输出均为float数组，格式：r0,i0,r1,i1...）
 */
void ne10_fft_c2c_1d_inverse_float32_array_mxu (float *fout, float *fin, ne10_fft_r2c_cfg_float32_t cfg)
{
    ne10_int32_t nfft = cfg->nfft;

    (void)nfft;

    ne10_fft_cpx_float32_t *fin_cpx = (ne10_fft_cpx_float32_t*)fin;
    ne10_fft_cpx_float32_t *fout_cpx = (ne10_fft_cpx_float32_t*)fout;
    ne10_fft_c2c_1d_inverse_float32_mxu_ai (fout_cpx, fin_cpx, cfg);
}
