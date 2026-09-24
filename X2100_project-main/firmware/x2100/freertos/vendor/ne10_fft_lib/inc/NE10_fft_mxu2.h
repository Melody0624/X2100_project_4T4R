#ifndef _NE10_FFT_MXU2_H_
#define _NE10_FFT_MXU2_H_
#include <mxu2.h>
#include "NE10_types.h"

#define SIMD_INLINE __attribute__ ((always_inline))
#define MXU_A_SIZE (128 / 8)

template <typename T> class CPLX;
template <typename T> class REAL;

enum dir_type
{
	INORDER = 0,
	ORDER = 1,
};
enum calc_op{
	madd = 0,
	msub,
	mul,
	neg_mul,
	add,
	sub,
	neg_equal,
	equal,
	noop
};

template <typename T>
class REALBase
{
protected:
    T val;
public:
	T& getValue(){
		return val;
	}
	void setValue(T& v){
		val = v;
	}
    SIMD_INLINE REALBase<T> operator= (REALBase<T> a)
		{
			val = a;
			return *this;
		}
};
template<>
class REAL<v4f32>: public REALBase<v4f32>{
public:
	REAL<v4f32>()
	{
	}
	REAL<v4f32>(float re)
	{
		val = _mx128_mffpu_w(re);
	}
};
template<int offset,int is_store>
static SIMD_INLINE void Prefetch(void *base)
{
	if(is_store)
		asm volatile ("pref 30,%0(%1) \t\n"::"i" (offset),"r" (base));
	else
		asm volatile ("pref 0,%0(%1) \t\n"::"i" (offset),"r" (base));
}

template <typename T>
class CPLXBase
{
protected:
    T val[2];
public:
	template<int offset>
	void SIMD_INLINE Load(void *base)
    {
		v16i8 a = _mx128_lu1q((void*)base,offset);
		val[0] = *(T*)&a;
		a = _mx128_lu1q((void*)base,offset + MXU_A_SIZE);
		val[1] = *(T*)&a;
		//Prefetch<128 + offset,0>(base);
    }

	void SIMD_INLINE Load(void *base,int offset)
    {
		v16i8 a = _mx128_lu1qx((void*)base,offset);
		val[0] = *(T*)&a;
		a = _mx128_lu1qx((void*)base,offset + MXU_A_SIZE);
		val[1] = *(T*)&a;
		//Prefetch<128,0>((char*)base + offset);
    }

	T& SIMD_INLINE GetReal()
	{
		return val[0];
	}

	T& SIMD_INLINE GetImage()
	{
		return val[1];
	}
	template<int offset>
		void SIMD_INLINE Store(void *base)
    {
		v16i8 d = *(v16i8 *) &val[0];
		_mx128_su1q(d,(void*)base,offset);
		d = *(v16i8 *) &val[1];
		_mx128_su1q(d,(void*)base,offset + MXU_A_SIZE);
    }

	void SIMD_INLINE Store(void *base,int offset)
    {
		v16i8 d = *(v16i8 *) &val[0];
		_mx128_su1qx(d,(void*)base,offset);
		d = *(v16i8 *) &val[1];
		_mx128_su1qx(d,(void*)base,offset + MXU_A_SIZE);
    }
	void SIMD_INLINE Conj(void){
		val[1] = -val[1];
	}
    SIMD_INLINE CPLXBase<T> operator= (CPLXBase<T> a)
		{
			val[0] = a.val[0];
			val[1] = a.val[1];
			return *this;
		}
};
template <>
class CPLX<v4f32> :public CPLXBase<v4f32>{
public:
    CPLX<v4f32>(){
    }
    CPLX<v4f32>(const ne10_float32_t re,const ne10_float32_t im)
    {
        val[0] = _mx128_mffpu_w(re);
        val[1] = _mx128_mffpu_w(im);
    }
	CPLX<v4f32>(ne10_fft_cpx_float32_t cplx)
	{
		val[0] = _mx128_mffpu_w(cplx.r);
        val[1] = _mx128_mffpu_w(cplx.i);
	}
	//dir 0      vector to real image ,1: real image to vector
    template<int dir>
		void SIMD_INLINE Transpose(void)
    {
        const v16i8 shv_lo[2] =
			{
				{0,2,4,6, 16,18,20,22, 1,3,5,7,    17,19,21,23},
				{0,2,4,6, 1,3,5,7,    8,10,12,14,   9,11,13,15},
			};
		const v16i8 shv_hi[2] = {
            {8,10,12,14,  24,26,28,30,   9,11,13,15,   25,27,29,31},
            {16,18,20,22, 17,19,21,23,   24,26,28,30,  25,27,29,31}
        };
		v16i8 i_even = *(v16i8*)&val[0];
		v16i8 i_odd = *(v16i8*)&val[1];

		v16i8 r0 = _mx128_shufv(i_odd,i_even,shv_lo[dir]);
		v16i8 r1 = _mx128_shufv(i_odd,i_even,shv_hi[dir]);
		val[0] = *(v4f32*)&r0;
		val[1] = *(v4f32*)&r1;
	}

    void SIMD_INLINE Mul(CPLX<v4f32>& a)
    {
        v4f32 r,i;
        r = val[0] * a.val[0];
        i = val[0] * a.val[1];

        r = _mx128_fmsub_w(r,val[1],a.val[1]);
		i = _mx128_fmadd_w(i,val[1],a.val[0]);
		val[0] = r;
		val[1] = i;
    }

	CPLX<v4f32> SIMD_INLINE RealMul(CPLX<v4f32>& a)
    {
		CPLX<v4f32> r;
        r.val[0] = val[0] * a.val[0];
        r.val[1] = val[0] * a.val[1];
		return r;
    }
	CPLX<v4f32>& SIMD_INLINE MultiSum(CPLX<v4f32>& sum,CPLX<v4f32>& a)
    {
		sum.val[0] = _mx128_fmsub_w(sum.val[0],val[1],a.val[1]);
		sum.val[1] = _mx128_fmadd_w(sum.val[1],val[1],a.val[0]);
		return sum;
	}

	CPLX<v4f32> SIMD_INLINE MulReal(CPLX<v4f32>& a)
    {
		CPLX<v4f32> r;
        r.val[0] = val[0] * a.val[0];
        r.val[1] = val[1] * a.val[0];
		return r;
    }

	// friend SIMD_INLINE CPLX<v4f32> ConjMul (CPLX<v4f32> a,CPLX<v4f32> b)
    // {
	// 	CPLX<v4f32> r;
	// 	r.val[0] = a.val[0] * b.val[0];
	// 	r.val[1] = a.val[1] * b.val[0];

	// 	r.val[0] = _mx128_fmadd_w(r.val[0],a.val[1],b.val[1]);
	// 	r.val[1] = _mx128_fmsub_w(r.val[1],a.val[0],b.val[1]);

    //     return r;
    // }


	CPLX<v4f32>& SIMD_INLINE ConjMultiSum(CPLX<v4f32>& sum,CPLX<v4f32>& a)
    {
		sum.val[0] = _mx128_fmadd_w(sum.val[0],val[1],a.val[1]);
		sum.val[1] = _mx128_fmsub_w(sum.val[1],val[0],a.val[1]);
		return sum;
	}

	template<dir_type dir,calc_op real,calc_op image>
	SIMD_INLINE void SIMD_INLINE Calculate(CPLX<v4f32> data,REAL<v4f32> a)
	{
#define CALCULATE(op,sid,did,v)											\
		do{																\
			switch(op)													\
			{															\
			case madd:													\
				val[sid] = _mx128_fmadd_w(val[sid],data.val[did],v);	\
				break;													\
			case msub:													\
				val[sid] = _mx128_fmsub_w(val[sid],data.val[did],v);	\
				break;													\
			case mul:													\
				val[sid] = data.val[did] * v;							\
				break;													\
			case neg_mul:												\
				val[sid] = -data.val[did] * v;							\
				break;													\
			}															\
		}while(0)

		CALCULATE(real,0,1 - dir,a.getValue());
		CALCULATE(image,1,dir,a.getValue());
#undef CALCULATE
	}


    void SIMD_INLINE Scale(REAL<v4f32> r)
    {
        val[0] *= r.getValue();
        val[1] *= r.getValue();
    }


	void dump_data(const char *s = NULL)
	{
		float* f0 = (float *) &val[0];
		float* f1 = (float *) &val[1];
		if(s){
			printf("%s->\n",s);
		}
		for(int i = 0;i < 4;i++)
		{
			printf("val 0:%f,\t1:%f\n",f0[i],f1[i]);
		}
		printf("\n");
	}
	template <dir_type dir,calc_op re,calc_op im>
		friend SIMD_INLINE CPLX<v4f32> CplxCalculate (CPLX<v4f32> a,CPLX<v4f32> b)
	{
		CPLX<v4f32> r;
#define CALCULATE(op,sid,did) do{						\
			switch(op)									\
			{											\
			case add:									\
				r.val[sid] = a.val[sid] + b.val[did];	\
				break;									\
			case sub:									\
				r.val[sid] = a.val[sid] - b.val[did];	\
				break;									\
			}											\
		}while(0)

		CALCULATE(re,0,1 - dir);
		CALCULATE(im,1,dir);

#undef CALCULATE
        return r;
	}

	template <dir_type dir,calc_op re,calc_op im>
		friend SIMD_INLINE CPLX<v4f32> CplxCalculate (CPLX<v4f32> v)
	{
		CPLX<v4f32> r;
#define CALCULATE(op,sid,did) do{				\
			switch(op)							\
			{									\
			case neg_equal:						\
				r.val[sid] = -v.val[did];		\
				break;							\
			case equal:							\
				r.val[sid] = v.val[did];		\
			}									\
		}while(0)

		CALCULATE(re,0,1 - dir);
		CALCULATE(im,1,dir);

#undef CALCULATE
        return r;
	}
	friend SIMD_INLINE void Radix4x4C_Transpose_Inteleave(CPLX<v4f32> out[4],CPLX<v4f32> in[4]){
		// //a0 b0 a2 b2
		// const v16i8 shv_even = {0,2,4,6,    1,3,5,7,  16,18,20,22, 17,19,21,23 };
		// //a1 b1 a3 b3
		// const v16i8 shv_odd  = {8,10,12,14, 9,11,13,15,  24,26,28,30, 25,27,29,31};

		//a0 a1 b0 b1
        const v16i8 shv_lo = {0,2,4,6,    8,10,12,14,     1,3,5,7,    9,11,13,15};
		//a2 a3 b2 b3
		const v16i8 shv_hi = {16,18,20,22, 24,26,28,30,17,19,21,23, 25,27,29,31};

		in[0].Transpose<1> ();
		in[1].Transpose<1> ();
		in[2].Transpose<1> ();
		in[3].Transpose<1> ();

		v16i8 a0 = *(v16i8 *)&in[0].val[0];
		v16i8 a2 = *(v16i8 *)&in[0].val[1];

		v16i8 b0 = *(v16i8 *)&in[1].val[0];
		v16i8 b2 = *(v16i8 *)&in[1].val[1];

		v16i8 c0 = *(v16i8 *)&in[2].val[0];
		v16i8 c2 = *(v16i8 *)&in[2].val[1];

		v16i8 d0 = *(v16i8 *)&in[3].val[0];
		v16i8 d2 = *(v16i8 *)&in[3].val[1];

		v16i8 a0b0 = _mx128_shufv(b0,a0,shv_lo);
		v16i8 a1b1 = _mx128_shufv(b0,a0,shv_hi);

		v16i8 c0d0 = _mx128_shufv(d0,c0,shv_lo);
		v16i8 c1d1 = _mx128_shufv(d0,c0,shv_hi);

		v16i8 a2b2 = _mx128_shufv(b2,a2,shv_lo);
		v16i8 a3b3 = _mx128_shufv(b2,a2,shv_hi);

		v16i8 c2d2 = _mx128_shufv(d2,c2,shv_lo);
		v16i8 c3d3 = _mx128_shufv(d2,c2,shv_hi);


		out[0].val[0] = *(v4f32 *)&a0b0;
		out[0].val[1] = *(v4f32 *)&c0d0;
		out[1].val[0] = *(v4f32 *)&a1b1;
		out[1].val[1] = *(v4f32 *)&c1d1;
		out[2].val[0] = *(v4f32 *)&a2b2;
		out[2].val[1] = *(v4f32 *)&c2d2;
		out[3].val[0] = *(v4f32 *)&a3b3;
		out[3].val[1] = *(v4f32 *)&c3d3;

	}

	friend SIMD_INLINE void Radix4x4C_Transpose(CPLX<v4f32> out[4],CPLX<v4f32> in[4]){
		//a0 b0 a2 b2
		const v16i8 shv_even = {0,2,4,6,    1,3,5,7,  16,18,20,22, 17,19,21,23 };
		//a1 b1 a3 b3
		const v16i8 shv_odd  = {8,10,12,14, 9,11,13,15,  24,26,28,30, 25,27,29,31};

		//a0 a1 b0 b1
        const v16i8 shv_lo = {0,2,4,6,    8,10,12,14,     1,3,5,7,    9,11,13,15};
		//a2 a3 b2 b3
		const v16i8 shv_hi = {16,18,20,22, 24,26,28,30,17,19,21,23, 25,27,29,31};
		v16i8 r0 = *(v16i8 *)&in[0].val[0];
		v16i8 r1 = *(v16i8 *)&in[1].val[0];
		v16i8 r2 = *(v16i8 *)&in[2].val[0];
		v16i8 r3 = *(v16i8 *)&in[3].val[0];

		v16i8 i0 = *(v16i8 *)&in[0].val[1];
		v16i8 i1 = *(v16i8 *)&in[1].val[1];
		v16i8 i2 = *(v16i8 *)&in[2].val[1];
		v16i8 i3 = *(v16i8 *)&in[3].val[1];

		v16i8 r01_lo = _mx128_shufv(r1,r0,shv_even);
		v16i8 r01_hi = _mx128_shufv(r1,r0,shv_odd);
		v16i8 r23_lo = _mx128_shufv(r3,r2,shv_even);
		v16i8 r23_hi = _mx128_shufv(r3,r2,shv_odd);
		v16i8 i01_lo = _mx128_shufv(i1,i0,shv_even);
		v16i8 i01_hi = _mx128_shufv(i1,i0,shv_odd);
		v16i8 i23_lo = _mx128_shufv(i3,i2,shv_even);
		v16i8 i23_hi = _mx128_shufv(i3,i2,shv_odd);

		v16i8 r02_lo = _mx128_shufv(r23_lo,r01_lo,shv_lo);
		v16i8 i02_lo = _mx128_shufv(i23_lo,i01_lo,shv_lo);
		v16i8 r02_hi = _mx128_shufv(r23_hi,r01_hi,shv_lo);
		v16i8 i02_hi = _mx128_shufv(i23_hi,i01_hi,shv_lo);
		v16i8 r13_lo = _mx128_shufv(r23_lo,r01_lo,shv_hi);
		v16i8 i13_lo = _mx128_shufv(i23_lo,i01_lo,shv_hi);
		v16i8 r13_hi = _mx128_shufv(r23_hi,r01_hi,shv_hi);
		v16i8 i13_hi = _mx128_shufv(i23_hi,i01_hi,shv_hi);

		out[0].val[0] = *(v4f32 *)&r02_lo;
		out[0].val[1] = *(v4f32 *)&i02_lo;
		out[1].val[0] = *(v4f32 *)&r02_hi;
		out[1].val[1] = *(v4f32 *)&i02_hi;
		out[2].val[0] = *(v4f32 *)&r13_lo;
		out[2].val[1] = *(v4f32 *)&i13_lo;
		out[3].val[0] = *(v4f32 *)&r13_hi;
		out[3].val[1] = *(v4f32 *)&i13_hi;

	}


	friend SIMD_INLINE void Radix8x4C_Transpose_Inteleave(CPLX<v4f32> out[8],CPLX<v4f32> in[8]){
		//a0 a1 b0 b1
        const v16i8 shv_lo = {0,2,4,6,    8,10,12,14,     1,3,5,7,    9,11,13,15};
		//a2 a3 b2 b3
		const v16i8 shv_hi = {16,18,20,22, 24,26,28,30,17,19,21,23, 25,27,29,31};

		in[0].Transpose<1> ();
		in[1].Transpose<1> ();
		in[2].Transpose<1> ();
		in[3].Transpose<1> ();
		in[4].Transpose<1> ();
		in[5].Transpose<1> ();
		in[6].Transpose<1> ();
		in[7].Transpose<1> ();

		v16i8 a0 = *(v16i8 *)&in[0].val[0];
		v16i8 a2 = *(v16i8 *)&in[0].val[1];

		v16i8 b0 = *(v16i8 *)&in[1].val[0];
		v16i8 b2 = *(v16i8 *)&in[1].val[1];

		v16i8 c0 = *(v16i8 *)&in[2].val[0];
		v16i8 c2 = *(v16i8 *)&in[2].val[1];

		v16i8 d0 = *(v16i8 *)&in[3].val[0];
		v16i8 d2 = *(v16i8 *)&in[3].val[1];

		v16i8 e0 = *(v16i8 *)&in[4].val[0];
		v16i8 e2 = *(v16i8 *)&in[4].val[1];

		v16i8 f0 = *(v16i8 *)&in[5].val[0];
		v16i8 f2 = *(v16i8 *)&in[5].val[1];

		v16i8 g0 = *(v16i8 *)&in[6].val[0];
		v16i8 g2 = *(v16i8 *)&in[6].val[1];

		v16i8 h0 = *(v16i8 *)&in[7].val[0];
		v16i8 h2 = *(v16i8 *)&in[7].val[1];


		v16i8 a0b0 = _mx128_shufv(b0,a0,shv_lo);
		v16i8 a1b1 = _mx128_shufv(b0,a0,shv_hi);

		v16i8 c0d0 = _mx128_shufv(d0,c0,shv_lo);
		v16i8 c1d1 = _mx128_shufv(d0,c0,shv_hi);

		v16i8 e0f0 = _mx128_shufv(f0,e0,shv_lo);
		v16i8 e1f1 = _mx128_shufv(f0,e0,shv_hi);

		v16i8 g0h0 = _mx128_shufv(h0,g0,shv_lo);
		v16i8 g1h1 = _mx128_shufv(h0,g0,shv_hi);

		v16i8 a2b2 = _mx128_shufv(b2,a2,shv_lo);
		v16i8 a3b3 = _mx128_shufv(b2,a2,shv_hi);

		v16i8 c2d2 = _mx128_shufv(d2,c2,shv_lo);
		v16i8 c3d3 = _mx128_shufv(d2,c2,shv_hi);

		v16i8 e2f2 = _mx128_shufv(f2,e2,shv_lo);
		v16i8 e3f3 = _mx128_shufv(f2,e2,shv_hi);

		v16i8 g2h2 = _mx128_shufv(h2,g2,shv_lo);
		v16i8 g3h3 = _mx128_shufv(h2,g2,shv_hi);

		out[0].val[0] = *(v4f32 *)&a0b0;
		out[0].val[1] = *(v4f32 *)&c0d0;

		out[2].val[0] = *(v4f32 *)&a1b1;
		out[2].val[1] = *(v4f32 *)&c1d1;

		out[4].val[0] = *(v4f32 *)&a2b2;
		out[4].val[1] = *(v4f32 *)&c2d2;
		out[6].val[0] = *(v4f32 *)&a3b3;
		out[6].val[1] = *(v4f32 *)&c3d3;

		out[1].val[0] = *(v4f32 *)&e0f0;
		out[1].val[1] = *(v4f32 *)&g0h0;
		out[3].val[0] = *(v4f32 *)&e1f1;
		out[3].val[1] = *(v4f32 *)&g1h1;
		out[5].val[0] = *(v4f32 *)&e2f2;
		out[5].val[1] = *(v4f32 *)&g2h2;
		out[7].val[0] = *(v4f32 *)&e3f3;
		out[7].val[1] = *(v4f32 *)&g3h3;

	}

	friend SIMD_INLINE void Radix8x4C_Transpose(CPLX<v4f32> out[8],CPLX<v4f32> in[8]){
		//a0 b0 a2 b2
		const v16i8 shv_even = {0,2,4,6,    1,3,5,7,  16,18,20,22, 17,19,21,23 };
		//a1 b1 a3 b3
		const v16i8 shv_odd  = {8,10,12,14, 9,11,13,15,  24,26,28,30, 25,27,29,31};

		//a0 a1 b0 b1
        const v16i8 shv_lo = {0,2,4,6,    8,10,12,14,     1,3,5,7,    9,11,13,15};
		//a2 a3 b2 b3
		const v16i8 shv_hi = {16,18,20,22, 24,26,28,30,17,19,21,23, 25,27,29,31};
		v16i8 r0 = *(v16i8 *)&in[0].val[0];
		v16i8 r1 = *(v16i8 *)&in[1].val[0];
		v16i8 r2 = *(v16i8 *)&in[2].val[0];
		v16i8 r3 = *(v16i8 *)&in[3].val[0];
		v16i8 r4 = *(v16i8 *)&in[4].val[0];
		v16i8 r5 = *(v16i8 *)&in[5].val[0];
		v16i8 r6 = *(v16i8 *)&in[6].val[0];
		v16i8 r7 = *(v16i8 *)&in[7].val[0];

		v16i8 i0 = *(v16i8 *)&in[0].val[1];
		v16i8 i1 = *(v16i8 *)&in[1].val[1];
		v16i8 i2 = *(v16i8 *)&in[2].val[1];
		v16i8 i3 = *(v16i8 *)&in[3].val[1];
		v16i8 i4 = *(v16i8 *)&in[4].val[1];
		v16i8 i5 = *(v16i8 *)&in[5].val[1];
		v16i8 i6 = *(v16i8 *)&in[6].val[1];
		v16i8 i7 = *(v16i8 *)&in[7].val[1];

		v16i8 r01_lo = _mx128_shufv(r1,r0,shv_even);
		v16i8 r01_hi = _mx128_shufv(r1,r0,shv_odd);
		v16i8 r23_lo = _mx128_shufv(r3,r2,shv_even);
		v16i8 r23_hi = _mx128_shufv(r3,r2,shv_odd);
		v16i8 i01_lo = _mx128_shufv(i1,i0,shv_even);
		v16i8 i01_hi = _mx128_shufv(i1,i0,shv_odd);
		v16i8 i23_lo = _mx128_shufv(i3,i2,shv_even);
		v16i8 i23_hi = _mx128_shufv(i3,i2,shv_odd);

		v16i8 r45_lo = _mx128_shufv(r5,r4,shv_even);
		v16i8 r45_hi = _mx128_shufv(r5,r4,shv_odd);
		v16i8 r67_lo = _mx128_shufv(r7,r6,shv_even);
		v16i8 r67_hi = _mx128_shufv(r7,r6,shv_odd);
		v16i8 i45_lo = _mx128_shufv(i5,i4,shv_even);
		v16i8 i45_hi = _mx128_shufv(i5,i4,shv_odd);
		v16i8 i67_lo = _mx128_shufv(i7,i6,shv_even);
		v16i8 i67_hi = _mx128_shufv(i7,i6,shv_odd);

		v16i8 r02_lo = _mx128_shufv(r23_lo,r01_lo,shv_lo);
		v16i8 i02_lo = _mx128_shufv(i23_lo,i01_lo,shv_lo);
		v16i8 r02_hi = _mx128_shufv(r23_hi,r01_hi,shv_lo);
		v16i8 i02_hi = _mx128_shufv(i23_hi,i01_hi,shv_lo);
		v16i8 r13_lo = _mx128_shufv(r23_lo,r01_lo,shv_hi);
		v16i8 i13_lo = _mx128_shufv(i23_lo,i01_lo,shv_hi);
		v16i8 r13_hi = _mx128_shufv(r23_hi,r01_hi,shv_hi);
		v16i8 i13_hi = _mx128_shufv(i23_hi,i01_hi,shv_hi);

		v16i8 r46_lo = _mx128_shufv(r67_lo,r45_lo,shv_lo);
		v16i8 i46_lo = _mx128_shufv(i67_lo,i45_lo,shv_lo);

		v16i8 r46_hi = _mx128_shufv(r67_hi,r45_hi,shv_lo);
		v16i8 i46_hi = _mx128_shufv(i67_hi,i45_hi,shv_lo);

		v16i8 r57_lo = _mx128_shufv(r67_lo,r45_lo,shv_hi);
		v16i8 i57_lo = _mx128_shufv(i67_lo,i45_lo,shv_hi);
		v16i8 r57_hi = _mx128_shufv(r67_hi,r45_hi,shv_hi);
		v16i8 i57_hi = _mx128_shufv(i67_hi,i45_hi,shv_hi);

		out[0].val[0] = *(v4f32 *)&r02_lo;
		out[0].val[1] = *(v4f32 *)&i02_lo;
		out[2].val[0] = *(v4f32 *)&r02_hi;
		out[2].val[1] = *(v4f32 *)&i02_hi;
		out[4].val[0] = *(v4f32 *)&r13_lo;
		out[4].val[1] = *(v4f32 *)&i13_lo;
		out[6].val[0] = *(v4f32 *)&r13_hi;
		out[6].val[1] = *(v4f32 *)&i13_hi;

		out[1].val[0] = *(v4f32 *)&r46_lo;
		out[1].val[1] = *(v4f32 *)&i46_lo;
		out[3].val[0] = *(v4f32 *)&r46_hi;
		out[3].val[1] = *(v4f32 *)&i46_hi;
		out[5].val[0] = *(v4f32 *)&r57_lo;
		out[5].val[1] = *(v4f32 *)&i57_lo;
		out[7].val[0] = *(v4f32 *)&r57_hi;
		out[7].val[1] = *(v4f32 *)&i57_hi;

	}


    friend SIMD_INLINE CPLX<v4f32> operator+ (CPLX<v4f32> a,CPLX<v4f32> b)
    {
        return CplxCalculate<ORDER,add,add> (a, b);
    }
    friend SIMD_INLINE CPLX<v4f32> operator- (CPLX<v4f32> a,CPLX<v4f32> b)
    {
        return CplxCalculate<ORDER,sub,sub> (a, b);;
    }

    friend SIMD_INLINE CPLX<v4f32> operator* (CPLX<v4f32> a,CPLX<v4f32> b)
    {
		CPLX<v4f32> r;
		r.val[0] = a.val[0] * b.val[0];
		r.val[1] = a.val[0] * b.val[1];
		r.val[0] = _mx128_fmsub_w(r.val[0],a.val[1],b.val[1]);
		r.val[1] = _mx128_fmadd_w(r.val[1],a.val[1],b.val[0]);

        return r;
    }
	friend SIMD_INLINE CPLX<v4f32> ConjMul (CPLX<v4f32> a,CPLX<v4f32> b)
    {
		CPLX<v4f32> r;
		r.val[0] = a.val[0] * b.val[0];
		r.val[1] = a.val[1] * b.val[0];

		r.val[0] = _mx128_fmadd_w(r.val[0],a.val[1],b.val[1]);
		r.val[1] = _mx128_fmsub_w(r.val[1],a.val[0],b.val[1]);

        return r;
    }

    friend SIMD_INLINE CPLX<v4f32> InorderAdd (CPLX<v4f32> a,CPLX<v4f32> b)
    {
        return CplxCalculate<INORDER,add,sub>(a,b);
    }
    friend SIMD_INLINE CPLX<v4f32> InorderSub (CPLX<v4f32> a,CPLX<v4f32> b)
    {
        return CplxCalculate<INORDER,sub,add>(a,b);
    }
};

template<int radix>
void SIMD_INLINE fft_inplace(CPLX<v4f32> *scratch_out);

template<>
void SIMD_INLINE fft_inplace<4>(CPLX<v4f32> *scratch_out)
{
    CPLX<v4f32> scratch[4];

    scratch[0] = scratch_out[0] + scratch_out[2];
    scratch[1] = scratch_out[0] - scratch_out[2];

    scratch[2] = scratch_out[1] + scratch_out[3];
    scratch[3] = scratch_out[1] - scratch_out[3];

    scratch_out[2] = scratch[0] - scratch[2];
    scratch_out[0] = scratch[0] + scratch[2];

    scratch_out[1] = InorderAdd(scratch[1], scratch[3]);
    scratch_out[3] = InorderSub(scratch[1], scratch[3]);
}
template<>
void SIMD_INLINE fft_inplace<5> (CPLX<v4f32> Fout[5])
{
    CPLX<v4f32> s[6];

	s[1] = Fout[1] + Fout[4];
	s[2] = Fout[2] + Fout[3];
    s[0] = Fout[0];
    s[5] = Fout[0];

    Fout[0] = Fout[0] + s[1] + s[2];

	REAL<v4f32> tw_5a_r(TW_5A_F32.r);
	REAL<v4f32> tw_5a_i(TW_5A_F32.i);

	s[0].Calculate<ORDER,madd,madd>(s[1],tw_5a_r);
	s[5].Calculate<ORDER,madd,madd>(s[1],tw_5a_r);

	s[0].Calculate<ORDER,madd,madd>(s[2],tw_5a_r);
	s[5].Calculate<ORDER,madd,madd>(s[2],tw_5a_r);

	s[4] = Fout[1] - Fout[4];
	s[3] = Fout[2] - Fout[3];

	s[1].Calculate<INORDER,mul,neg_mul>(s[4],tw_5a_i);
	s[2].Calculate<INORDER,neg_mul,mul>(s[4],tw_5a_i);

	s[1].Calculate<INORDER,madd,msub>(s[3],tw_5a_i);
	s[2].Calculate<INORDER,madd,msub>(s[3],tw_5a_i);

	Fout[1] = s[0] - s[1];
	Fout[4] = s[0] + s[1];
	Fout[2] = s[5] + s[2];
	Fout[3] = s[5] - s[2];
}


template<int radix>
SIMD_INLINE void fft_func(CPLX<v4f32> out[radix],const CPLX<v4f32> in[radix]);


template<>
SIMD_INLINE void fft_func<8>(CPLX<v4f32> out[8],const CPLX<v4f32> in[8])
{
    CPLX<v4f32> s[8];
    // const static ne10_fft_cpx_float32_t TW_8[4] =
	// 	{
	// 		{  1.00000,  0.00000 },
	// 		{  0.70711, -0.70711 },
	// 		{  0.00000, -1.00000 },
	// 		{ -0.70711, -0.70711 },
	// 	};

	CPLX<v4f32> tw0(1.00000,  0.00000);
	CPLX<v4f32> tw1(0.70711, -0.70711);
	CPLX<v4f32> tw2(0.00000, -1.00000);
	CPLX<v4f32> tw3(-0.70711, -0.70711);

    // STAGE - 1
    // in -> s


	s[0] = in[0] + in[4];
	s[4] = in[0] - in[4];

	s[1] = in[1] + in[5];
	s[5] = in[1] - in[5];

	s[2] = in[2] + in[6];
	s[6] = in[2] - in[6];

	s[3] = in[3] + in[7];
	s[7] = in[3] - in[7];

	// STAGE - 2
    // s -> out

	s[4].Mul(tw0);
	s[5].Mul(tw1);
	s[6].Mul(tw2);
	s[7].Mul(tw3);

	out[0] = s[0] + s[2];
	out[2] = s[0] - s[2];

	out[1] = s[1] + s[3];
	out[3] = s[1] - s[3];

	out[4] = s[4] + s[6];
	out[6] = s[4] - s[6];

	out[5] = s[5] + s[7];
	out[7] = s[5] - s[7];

    // STAGE - 3
    // out -> s
    {
        // TW
		out[2].Mul(tw0);
		out[3].Mul(tw2);
		out[6].Mul(tw0);
		out[7].Mul(tw2);

		s[0] = out[0] + out[1];
		s[4] = out[0] - out[1];

		s[2] = out[2] + out[3];
		s[6] = out[2] - out[3];

		s[1] = out[4] + out[5];
		s[5] = out[4] - out[5];

		s[3] = out[6] + out[7];
		s[7] = out[6] - out[7];
    }
    out[0] = s[0];
    out[1] = s[1];
    out[2] = s[2];
    out[3] = s[3];
    out[4] = s[4];
    out[5] = s[5];
    out[6] = s[6];
    out[7] = s[7];

}

#endif /* _NE10_FFT_MXU2_H_ */
