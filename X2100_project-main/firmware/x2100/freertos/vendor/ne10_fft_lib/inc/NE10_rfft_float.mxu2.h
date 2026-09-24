#ifndef __NE10_RFFT_FLOAT32_MXU2_H__
#define __NE10_RFFT_FLOAT32_MXU2_H__

#include <mxu2.h>
#define MXU_A_SIZE (128 / 8)
#define CONST_TW_81   0.70710678
#define CONST_TW_81N -0.70710678

#define DIV_TW81   1.4142136f
#define DIV_TW81N -1.4142136f

typedef struct
{
  v4f32 val[2];
}CPLX;
#define FILL_F(f) ({_mx128_mffpu_w(f);})

#define CPLX_FILL(cplxA,re,im)			\
  do{						\
    cplxA.val[0] = FILL_F(re);			\
    cplxA.val[1] = FILL_F(im);			\
  }while(0)
#define CPLX_LOAD(cplx,base, offset)	do{		\
    v16i8 a = _mx128_lu1q((void*)base,offset);		\
    cplx.val[0] = *(v4f32*)&a;				\
    a = _mx128_lu1q((void*)base,offset + MXU_A_SIZE);	\
    cplx.val[1] = *(v4f32*)&a;				\
  }while(0)

#define CPLX_LOADX(cplx,base, offset)			\
  do{							\
    v16i8 a = _mx128_lu1qx((void*)base,offset);		\
    cplx.val[0] = *(v4f32*)&a;				\
    a = _mx128_lu1qx((void*)base,offset + MXU_A_SIZE);	\
    cplx.val[1] = *(v4f32*)&a;				\
  }while(0)

#define CPLX_STORE(cplx,base,offset)	do{		\
    v16i8 d = *(v16i8 *) &cplx.val[0];			\
    _mx128_su1q(d,(void*)base,offset);			\
    d = *(v16i8 *) &cplx.val[1];			\
    _mx128_su1q(d,(void*)base,offset + MXU_A_SIZE);	\
  }while(0)

#define CPLX_STOREX(cplx,base,offset)	do{		\
    v16i8 d = *(v16i8 *) &cplx.val[0];			\
    _mx128_su1qx(d,(void*)base,offset);			\
    d = *(v16i8 *) &cplx.val[1];			\
    _mx128_su1qx(d,(void*)base,offset + MXU_A_SIZE);	\
  }while(0)

#define CPLX_TRANSPOSE(cplx,dir)				\
  do{								\
    const v16i8 shv_lo[2] = {					\
      {0,2,4,6, 16,18,20,22, 1,3,5,7,    17,19,21,23},		\
      {0,2,4,6, 1,3,5,7,    8,10,12,14,   9,11,13,15}};		\
    const v16i8 shv_hi[2] = {					\
      {8,10,12,14,  24,26,28,30,   9,11,13,15,   25,27,29,31},	\
      {16,18,20,22, 17,19,21,23,   24,26,28,30,  25,27,29,31}};	\
    v16i8 i_even = *(v16i8*)&cplx.val[0];			\
    v16i8 i_odd = *(v16i8*)&cplx.val[1];			\
    v16i8 r0 = _mx128_shufv(i_odd,i_even,shv_lo[dir]);		\
    v16i8 r1 = _mx128_shufv(i_odd,i_even,shv_hi[dir]);		\
    cplx.val[0] = *(v4f32*)&r0;					\
    cplx.val[1] = *(v4f32*)&r1;					\
  }while(0)

#define CPLX_MUL(cplxOut,cplxA,cplxB)			\
							\
  do{							\
     v4f32 r,i;						\
     r = cplxA.val[0] * cplxB.val[0];			\
     i = cplxA.val[0] * cplxB.val[1];			\
     r = _mx128_fmsub_w(r,cplxA.val[1],cplxB.val[1]);	\
     i = _mx128_fmadd_w(i,cplxA.val[1],cplxB.val[0]);	\
     cplxOut.val[0] = r;				\
     cplxOut.val[1] = i;				\
     }while(0)

#define CPLX_INV_MUL(cplxOut,cplxA,cplxB)		\
							\
  do{							\
     v4f32 r,i;						\
     r = cplxA.val[0] * cplxB.val[0];			\
     i = cplxA.val[1] * cplxB.val[0];			\
     r = _mx128_fmadd_w(r,cplxA.val[1],cplxB.val[1]);	\
     i = _mx128_fmsub_w(i,cplxA.val[0],cplxB.val[1]);	\
     cplxOut.val[0] = r;				\
     cplxOut.val[1] = i;				\
     }while(0)


#define CPLX_MUL_REAL(cplxOut,cplxA,real)	\
  do{						\
    cplxOut.val[0] = cplxA.val[0] * real;	\
    cplxOut.val[1] = cplxA.val[1] * real;	\
  }while(0)

#define CPLX_ADD(cplxOut,cplxA,cplxB)			\
  do{							\
    cplxOut.val[0] = cplxA.val[0] + cplxB.val[0];	\
    cplxOut.val[1] = cplxA.val[1] + cplxB.val[1];	\
  }while(0)

#define CPLX_SUB(cplxOut,cplxA,cplxB)			\
  do{							\
    cplxOut.val[0] = cplxA.val[0] - cplxB.val[0];	\
    cplxOut.val[1] = cplxA.val[1] - cplxB.val[1];	\
  }while(0)

#define CPLX_INORDER_ADD(cplxOut,cplxA,cplxB)		\
    do{							\
      cplxOut.val[0] = cplxA.val[0] + cplxB.val[1];	\
      cplxOut.val[1] = cplxA.val[1] - cplxB.val[0];	\
    }while(0)

#define CPLX_INORDER_SUB(cplxOut,cplxA,cplxB)		\
    do{							\
      cplxOut.val[0] = cplxA.val[0] - cplxB.val[1];	\
      cplxOut.val[1] = cplxA.val[1] + cplxB.val[0];	\
    }while(0)

#define CPLX_MUL_CONJ(cplxOut,cplxA,cplxB)				\
    do{									\
      cplxOut.val[0] = cplxA.val[0] * cplxB.Val[0];			\
      cplxOut.val[1] = cplxA.val[1] * cplxB.Val[0];			\
      cplxOut.val[0] = _mx128_fmadd_w(cplxOut.val[0],cplxA.val[1],cplxB.val[1]); \
      cplxOut.val[1] = _mx128_fmsub_w(cplxOut.val[1],cplxA.val[0],cplxB.Val[1]); \
    }while(0)

#define LOAD_F(base, offset)	({			\
	v16i8 a = _mx128_lu1q((void*)base,offset);	\
	*(v4f32*)&a;					\
      })

#define LOADX_F(base, offset) ({			\
	v16i8 a = _mx128_lu1qx((void*)base,offset);	\
	*(v4f32*)&a;					\
      })

#define STORE_F(val,base,offset)	do{	\
      v16i8 d = *(v16i8 *) &val;		\
      _mx128_su1q(d,(void*)base,offset);	\
    }while(0)

#define STOREX_F(val,base,offset)	do{	\
      v16i8 d = *(v16i8 *) &val;		\
      _mx128_su1qx(d,(void*)base,offset);	\
    }while(0)


#define NE10_DECLARE_2(TYPE,NAME)   TYPE NAME ## 0;	\
    TYPE NAME ## 1;

#define NE10_DECLARE_3(TYPE,NAME)   NE10_DECLARE_2(TYPE,NAME);  \
    TYPE NAME ## 2;

#define NE10_DECLARE_4(TYPE,NAME)   NE10_DECLARE_3(TYPE,NAME);  \
    TYPE NAME ## 3;

#define NE10_DECLARE_8(TYPE,NAME)   NE10_DECLARE_4(TYPE,NAME);  \
    TYPE NAME ## 4;						\
    TYPE NAME ## 5;						\
    TYPE NAME ## 6;						\
    TYPE NAME ## 7;


#define NE10_RADIX4x4_R2C_MXU_LOAD(PTR_IN,Q_IN,IN_STEP)			\
    do {								\
      Q_IN ## 0 = LOAD_F( (void*) ( PTR_IN ) ,0 * IN_STEP * MXU_A_SIZE); \
      Q_IN ## 1 = LOAD_F( (void*) ( PTR_IN ) ,1 * IN_STEP * MXU_A_SIZE); \
      Q_IN ## 2 = LOAD_F( (void*) ( PTR_IN ) ,2 * IN_STEP * MXU_A_SIZE); \
      Q_IN ## 3 = LOAD_F( (void*) ( PTR_IN ) ,3 * IN_STEP * MXU_A_SIZE); \
    }while(0)

#define NE10_RADIX8x4_R2C_MXU_LOAD(PTR_IN,Q_IN,IN_STEP)			\
    do {								\
      Q_IN ## 0 = LOAD_F( (void*) ( PTR_IN ) ,0 * IN_STEP * MXU_A_SIZE); \
      Q_IN ## 1 = LOAD_F( (void*) ( PTR_IN ) ,1 * IN_STEP * MXU_A_SIZE); \
      Q_IN ## 2 = LOAD_F( (void*) ( PTR_IN ) ,2 * IN_STEP * MXU_A_SIZE); \
      Q_IN ## 3 = LOAD_F( (void*) ( PTR_IN ) ,3 * IN_STEP * MXU_A_SIZE); \
      Q_IN ## 4 = LOAD_F( (void*) ( PTR_IN ) ,4 * IN_STEP * MXU_A_SIZE); \
      Q_IN ## 5 = LOAD_F( (void*) ( PTR_IN ) ,5 * IN_STEP * MXU_A_SIZE); \
      Q_IN ## 6 = LOAD_F( (void*) ( PTR_IN ) ,6 * IN_STEP * MXU_A_SIZE); \
      Q_IN ## 7 = LOAD_F( (void*) ( PTR_IN ) ,7 * IN_STEP * MXU_A_SIZE); \
    } while(0)

#define NE10_RADIX4x4_R2C_MXU_LOADX(PTR_IN,Q_IN,IN_STEP)		\
    do {								\
	Q_IN ## 0 = LOADX_F( (void*) ( PTR_IN ) ,0 * IN_STEP * MXU_A_SIZE); \
	Q_IN ## 1 = LOADX_F( (void*) ( PTR_IN ) ,1 * IN_STEP * MXU_A_SIZE); \
	Q_IN ## 2 = LOADX_F( (void*) ( PTR_IN ) ,2 * IN_STEP * MXU_A_SIZE); \
	Q_IN ## 3 = LOADX_F( (void*) ( PTR_IN ) ,3 * IN_STEP * MXU_A_SIZE); \
	}while(0)

#define NE10_RADIX8x4_R2C_MXU_LOADX(PTR_IN,Q_IN,IN_STEP)		\
    do {								\
	Q_IN ## 0 = LOADX_F( (void*) ( PTR_IN ) ,0 * IN_STEP * MXU_A_SIZE); \
	Q_IN ## 1 = LOADX_F( (void*) ( PTR_IN ) ,1 * IN_STEP * MXU_A_SIZE); \
	Q_IN ## 2 = LOADX_F( (void*) ( PTR_IN ) ,2 * IN_STEP * MXU_A_SIZE); \
	Q_IN ## 3 = LOADX_F( (void*) ( PTR_IN ) ,3 * IN_STEP * MXU_A_SIZE); \
	Q_IN ## 4 = LOADX_F( (void*) ( PTR_IN ) ,4 * IN_STEP * MXU_A_SIZE); \
	Q_IN ## 5 = LOADX_F( (void*) ( PTR_IN ) ,5 * IN_STEP * MXU_A_SIZE); \
	Q_IN ## 6 = LOADX_F( (void*) ( PTR_IN ) ,6 * IN_STEP * MXU_A_SIZE); \
	Q_IN ## 7 = LOADX_F( (void*) ( PTR_IN ) ,7 * IN_STEP * MXU_A_SIZE); \
	} while(0);


#define NE10_RADIX8x4_R2C_MXU_KERNEL_S1(Q_OUT,Q_IN)	\
    do {						\
	const v4f32 Q_TW_81    = FILL_F(CONST_TW_81 );	\
	const v4f32 Q_TW_81N   = FILL_F(CONST_TW_81N);	\
	Q_OUT ## 0 = Q_IN ## 0 + Q_IN ## 4;		\
	Q_OUT ## 1 = Q_IN ## 0 - Q_IN ## 4;		\
	Q_OUT ## 2 = Q_IN ## 1 + Q_IN ## 5;		\
	Q_OUT ## 3 = Q_IN ## 1 - Q_IN ## 5;		\
	Q_OUT ## 4 = Q_IN ## 2 + Q_IN ## 6;		\
	Q_OUT ## 5 = Q_IN ## 2 - Q_IN ## 6;		\
	Q_OUT ## 6 = Q_IN ## 3 + Q_IN ## 7;		\
	Q_OUT ## 7 = Q_IN ## 3 - Q_IN ## 7;		\
	Q_OUT ## 3 = Q_OUT ## 3 * Q_TW_81 ;		\
	Q_OUT ## 7 = Q_OUT ## 7 * Q_TW_81N;		\
	} while(0);

#define NE10_RADIX8x4_R2C_MXU_KERNEL_S2(Q_OUT,Q_IN)	\
    do {						\
	NE10_DECLARE_4(v4f32,Q_S);			\
	Q_S0 =  Q_IN ## 0 + Q_IN ## 4;			\
	Q_S1 =	Q_IN ## 2 + Q_IN ## 6;			\
	Q_S2 =	Q_IN ## 7 - Q_IN ## 3;			\
	Q_S3 =	Q_IN ## 3 + Q_IN ## 7;			\
	Q_OUT ## 0 = 	   Q_S0 +	   Q_S1;	\
	Q_OUT ## 1 =  Q_IN ## 1 +	   Q_S3;	\
	Q_OUT ## 2 =	   Q_S2 - Q_IN ## 5;		\
	Q_OUT ## 3 =  Q_IN ## 0 - Q_IN ## 4;		\
	Q_OUT ## 4 =  Q_IN ## 6 - Q_IN ## 2;		\
	Q_OUT ## 5 =  Q_IN ## 1 -	   Q_S3;	\
	Q_OUT ## 6 =  Q_IN ## 5 +	   Q_S2;	\
	Q_OUT ## 7 =	   Q_S0 -	   Q_S1;	\
	} while(0)


#define NE10_RADIX4x4_R2C_MXU_KERNEL(Q_OUT,Q_IN)	\
    do {						\
      NE10_DECLARE_4(v4f32,Q_S_IN);			\
      Q_S_IN0 = Q_IN##0 + Q_IN##2;			\
      Q_S_IN1 = Q_IN##1 + Q_IN##3;			\
      Q_OUT##0 = Q_S_IN0 + Q_S_IN1;			\
      Q_OUT##1 = Q_IN##0 - Q_IN##2;			\
      Q_OUT##2 = Q_IN##3 - Q_IN##1;			\
      Q_OUT##3 = Q_S_IN0 - Q_S_IN1;			\
    } while(0)

#define NE10_RADIX4x4_C2R_MXU_KERNEL(Q_OUT,Q_IN)	\
    do {						\
      NE10_DECLARE_4(v4f32,Q_S_IN);			\
      Q_S_IN0 = Q_IN##0 + Q_IN##3;			\
      Q_S_IN1 = Q_IN##0 - Q_IN##3;			\
      Q_S_IN2 = Q_IN##1 + Q_IN##1;			\
      Q_S_IN3 = Q_IN##2 + Q_IN##2;			\
      Q_OUT ## 0 = Q_S_IN0 + Q_S_IN2;			\
      Q_OUT ## 1 = Q_S_IN1 - Q_S_IN3;			\
      Q_OUT ## 2 = Q_S_IN0 - Q_S_IN2;			\
      Q_OUT ## 3 = Q_S_IN1 + Q_S_IN3;			\
    } while(0)

#define NE10_RADIX8x4_R2C_MXU_KERNEL(Q_OUT,Q_IN)	\
      do {						\
	NE10_DECLARE_8(v4f32,Q_S_IN);			\
	NE10_RADIX8x4_R2C_MXU_KERNEL_S1(Q_S_IN,Q_IN);	\
	NE10_RADIX8x4_R2C_MXU_KERNEL_S2(Q_OUT,Q_S_IN);	\
      } while(0);


#define NE10_RADIX4x4_R2C_MXU_STORE(PTR_OUT,Q_OUT,OUT_STEP)		\
      do {								\
	STORE_F(Q_OUT ## 0,(void*) ( PTR_OUT),	0 * OUT_STEP * MXU_A_SIZE); \
	STORE_F(Q_OUT ## 1,(void*) ( PTR_OUT),	1 * OUT_STEP * MXU_A_SIZE); \
	STORE_F(Q_OUT ## 2,(void*) ( PTR_OUT),	2 * OUT_STEP * MXU_A_SIZE); \
	STORE_F(Q_OUT ## 3,(void*) ( PTR_OUT),	3 * OUT_STEP * MXU_A_SIZE); \
      }while(0)

#define NE10_RADIX8x4_R2C_MXU_STORE(PTR_OUT,Q_OUT,OUT_STEP)		\
      do {								\
	STORE_F(Q_OUT ## 0,(void*) ( PTR_OUT),	0 * OUT_STEP * MXU_A_SIZE); \
	STORE_F(Q_OUT ## 1,(void*) ( PTR_OUT),	1 * OUT_STEP * MXU_A_SIZE); \
	STORE_F(Q_OUT ## 2,(void*) ( PTR_OUT),	2 * OUT_STEP * MXU_A_SIZE); \
	STORE_F(Q_OUT ## 3,(void*) ( PTR_OUT),	3 * OUT_STEP * MXU_A_SIZE); \
	STORE_F(Q_OUT ## 4,(void*) ( PTR_OUT),	4 * OUT_STEP * MXU_A_SIZE); \
	STORE_F(Q_OUT ## 5,(void*) ( PTR_OUT),	5 * OUT_STEP * MXU_A_SIZE); \
	STORE_F(Q_OUT ## 6,(void*) ( PTR_OUT),	6 * OUT_STEP * MXU_A_SIZE); \
	STORE_F(Q_OUT ## 7,(void*) ( PTR_OUT),	7 * OUT_STEP * MXU_A_SIZE); \
      } while(0);


#define NE10_RADIX4x4_R2C_MXU_STOREX(PTR_OUT,Q_OUT,OUT_STEP)		\
      do {								\
	  STOREX_F(Q_OUT ## 0,(void*) ( PTR_OUT),	0 * OUT_STEP * MXU_A_SIZE); \
	  STOREX_F(Q_OUT ## 1,(void*) ( PTR_OUT),	1 * OUT_STEP * MXU_A_SIZE); \
	  STOREX_F(Q_OUT ## 2,(void*) ( PTR_OUT),	2 * OUT_STEP * MXU_A_SIZE); \
	  STOREX_F(Q_OUT ## 3,(void*) ( PTR_OUT),	3 * OUT_STEP * MXU_A_SIZE); \
	  }while(0)

#define NE10_RADIX8x4_R2C_MXU_STOREX(PTR_OUT,Q_OUT,OUT_STEP)		\
      do {								\
	  STOREX_F(Q_OUT ## 0,(void*) ( PTR_OUT),	0 * OUT_STEP * MXU_A_SIZE); \
	  STOREX_F(Q_OUT ## 1,(void*) ( PTR_OUT),	1 * OUT_STEP * MXU_A_SIZE); \
	  STOREX_F(Q_OUT ## 2,(void*) ( PTR_OUT),	2 * OUT_STEP * MXU_A_SIZE); \
	  STOREX_F(Q_OUT ## 3,(void*) ( PTR_OUT),	3 * OUT_STEP * MXU_A_SIZE); \
	  STOREX_F(Q_OUT ## 4,(void*) ( PTR_OUT),	4 * OUT_STEP * MXU_A_SIZE); \
	  STOREX_F(Q_OUT ## 5,(void*) ( PTR_OUT),	5 * OUT_STEP * MXU_A_SIZE); \
	  STOREX_F(Q_OUT ## 6,(void*) ( PTR_OUT),	6 * OUT_STEP * MXU_A_SIZE); \
	  STOREX_F(Q_OUT ## 7,(void*) ( PTR_OUT),	7 * OUT_STEP * MXU_A_SIZE); \
	  } while(0);



#define NE10_RADIX4x4_C2R_MXU_KERNEL_SCALE_DATA(Q_OUT,v)	\
	do {							\
	  Q_OUT ## 0 =  Q_OUT ## 0 * v;				\
	  Q_OUT ## 1 =  Q_OUT ## 1 * v;				\
	  Q_OUT ## 2 =  Q_OUT ## 2 * v;				\
	  Q_OUT ## 3 =  Q_OUT ## 3 * v;				\
	}while(0)


#define NE10_RADIX8x4_C2R_MXU_KERNEL_SCALE_DATA(Q_OUT,v)	\
	do {							\
	  Q_OUT ## 0 =  Q_OUT ## 0 * v;				\
	  Q_OUT ## 1 =  Q_OUT ## 1 * v;				\
	  Q_OUT ## 2 =  Q_OUT ## 2 * v;				\
	  Q_OUT ## 3 =  Q_OUT ## 3 * v;				\
	  Q_OUT ## 4 =  Q_OUT ## 4 * v;				\
	  Q_OUT ## 5 =  Q_OUT ## 5 * v;				\
	  Q_OUT ## 6 =  Q_OUT ## 6 * v;				\
	  Q_OUT ## 7 =  Q_OUT ## 7 * v;				\
	} while(0);




#define NE10_RADIX8x4_C2R_MXU_KERNEL_S1(Q_OUT,Q_IN)	\
	do {						\
	  NE10_DECLARE_8(v4f32,Q_S_IN);			\
	  Q_S_IN0 = Q_IN ## 0 + Q_IN ## 7;		\
	  Q_S_IN1 = Q_IN ## 0 - Q_IN ## 7;		\
	  Q_S_IN2 = Q_IN ## 1 + Q_IN ## 5;		\
	  Q_S_IN3 = Q_IN ## 1 - Q_IN ## 5;		\
	  Q_S_IN4 = Q_IN ## 6 + Q_IN ## 2;		\
	  Q_S_IN5 = Q_IN ## 6 - Q_IN ## 2;		\
	  Q_S_IN6 = Q_IN ## 3 + Q_IN ## 3;		\
	  Q_S_IN7 = Q_IN ## 4 + Q_IN ## 4;		\
	  Q_OUT ## 0 = Q_S_IN0 + Q_S_IN6;		\
	  Q_OUT ## 1 = Q_S_IN2 + Q_S_IN2;		\
	  Q_OUT ## 2 = Q_S_IN1 - Q_S_IN7;		\
	  Q_OUT ## 3 = Q_S_IN3 - Q_S_IN4;		\
	  Q_OUT ## 4 = Q_S_IN0 - Q_S_IN6;		\
	  Q_OUT ## 5 = Q_S_IN5 + Q_S_IN5;		\
	  Q_OUT ## 6 = Q_S_IN1 + Q_S_IN7;		\
	  Q_OUT ## 7 = Q_S_IN4 + Q_S_IN3;		\
	} while (0);

#define NE10_RADIX8x4_C2R_MXU_KERNEL_S2(Q_OUT,Q_IN)		\
	do {							\
	    const v4f32 DIV_TW81_MXU    = FILL_F(DIV_TW81 );	\
	    const v4f32 DIV_TW81N_MXU   = FILL_F(DIV_TW81N);	\
	    Q_IN ## 3 = Q_IN ## 3 * DIV_TW81_MXU;		\
	    Q_IN ## 7 = Q_IN ## 7 * DIV_TW81N_MXU;		\
	    Q_OUT ## 0 = Q_IN ## 0 + Q_IN ## 1;			\
	    Q_OUT ## 4 = Q_IN ## 0 - Q_IN ## 1;			\
	    Q_OUT ## 1 = Q_IN ## 2 + Q_IN ## 3;			\
	    Q_OUT ## 5 = Q_IN ## 2 - Q_IN ## 3;			\
	    Q_OUT ## 2 = Q_IN ## 4 + Q_IN ## 5;			\
	    Q_OUT ## 6 = Q_IN ## 4 - Q_IN ## 5;			\
	    Q_OUT ## 3 = Q_IN ## 6 + Q_IN ## 7;			\
	    Q_OUT ## 7 = Q_IN ## 6 - Q_IN ## 7;			\
	    } while(0);



#define NE10_RADIX8x4_C2R_MXU_KERNEL(Q_OUT,Q_IN)			\
	do {								\
	    NE10_DECLARE_8(v4f32,Q_S_IN_C2R_KERNEL);			\
	    NE10_RADIX8x4_C2R_MXU_KERNEL_S1(Q_S_IN_C2R_KERNEL,Q_IN);	\
	    NE10_RADIX8x4_C2R_MXU_KERNEL_S2(Q_OUT,Q_S_IN_C2R_KERNEL);	\
	    } while(0);


#define NE10_REVERSE_V4F32(VECTOR4F)					\
	do {								\
	    v16i8 shv = {24,26,28,30,16,18,20,22,8,10,12,14,0,2,4,6};	\
	    v16i8 a = *(v16i8 *)&(VECTOR4F);				\
	    v16i8 r = _mx128_shufv(a,a,shv);				\
	    (VECTOR4F) = *(v4f32*)&r;					\
	    } while(0)

#define NE10_REVERSE_OUT_V4f32(VECTOR4F_OUT,VECTOR4F)			\
	do {								\
	    v16i8 shv = {24,26,28,30,16,18,20,22,8,10,12,14,0,2,4,6};	\
	    v16i8 a = *(v16i8 *)&VECTOR4F;				\
	    v16i8 r = _mx128_shufv(a,a,shv);				\
	    VECTOR4F_OUT = *(v4f32*)&r;					\
	    } while (0)

#define Radix4x4C_Transpose(out,in)					\
	do{								\
	   const v16i8 shv_even = {0,2,4,6,    1,3,5,7,  16,18,20,22, 17,19,21,23 }; \
	   const v16i8 shv_odd  = {8,10,12,14, 9,11,13,15,  24,26,28,30, 25,27,29,31}; \
	   const v16i8 shv_lo = {0,2,4,6,    8,10,12,14,     1,3,5,7,    9,11,13,15}; \
	   const v16i8 shv_hi = {16,18,20,22, 24,26,28,30,17,19,21,23, 25,27,29,31}; \
	   v16i8 r0 = *(v16i8 *)&(in ## 0).val[0];			\
	   v16i8 r1 = *(v16i8 *)&(in ## 1).val[0];			\
	   v16i8 r2 = *(v16i8 *)&(in ## 2).val[0];			\
	   v16i8 r3 = *(v16i8 *)&(in ## 3).val[0];			\
	   v16i8 i0 = *(v16i8 *)&(in ## 0).val[1];			\
	   v16i8 i1 = *(v16i8 *)&(in ## 1).val[1];			\
	   v16i8 i2 = *(v16i8 *)&(in ## 2).val[1];			\
	   v16i8 i3 = *(v16i8 *)&(in ## 3).val[1];			\
	   v16i8 r01_lo = _mx128_shufv(r1,r0,shv_even);			\
	   v16i8 r01_hi = _mx128_shufv(r1,r0,shv_odd);			\
	   v16i8 r23_lo = _mx128_shufv(r3,r2,shv_even);			\
	   v16i8 r23_hi = _mx128_shufv(r3,r2,shv_odd);			\
	   v16i8 i01_lo = _mx128_shufv(i1,i0,shv_even);			\
	   v16i8 i01_hi = _mx128_shufv(i1,i0,shv_odd);			\
	   v16i8 i23_lo = _mx128_shufv(i3,i2,shv_even);			\
	   v16i8 i23_hi = _mx128_shufv(i3,i2,shv_odd);			\
	   v16i8 r02_lo = _mx128_shufv(r23_lo,r01_lo,shv_lo);		\
	   v16i8 i02_lo = _mx128_shufv(i23_lo,i01_lo,shv_lo);		\
	   v16i8 r02_hi = _mx128_shufv(r23_hi,r01_hi,shv_lo);		\
	   v16i8 i02_hi = _mx128_shufv(i23_hi,i01_hi,shv_lo);		\
	   v16i8 r13_lo = _mx128_shufv(r23_lo,r01_lo,shv_hi);		\
	   v16i8 i13_lo = _mx128_shufv(i23_lo,i01_lo,shv_hi);		\
	   v16i8 r13_hi = _mx128_shufv(r23_hi,r01_hi,shv_hi);		\
	   v16i8 i13_hi = _mx128_shufv(i23_hi,i01_hi,shv_hi);		\
	   (out ## 0).val[0] = *(v4f32 *)&r02_lo;			\
	   (out ## 0).val[1] = *(v4f32 *)&i02_lo;			\
	   (out ## 1).val[0] = *(v4f32 *)&r02_hi;			\
	   (out ## 1).val[1] = *(v4f32 *)&i02_hi;			\
	   (out ## 2).val[0] = *(v4f32 *)&r13_lo;			\
	   (out ## 2).val[1] = *(v4f32 *)&i13_lo;			\
	   (out ## 3).val[0] = *(v4f32 *)&r13_hi;			\
	   (out ## 3).val[1] = *(v4f32 *)&i13_hi;			\
	   }while(0)

#define NE10_RADIX4x4_CPLX_LOADX(PTR_IN,Q_IN,IN_STEP)			\
	do {								\
	  CPLX_LOAD(Q_IN ## 0,PTR_IN,0);				\
	  CPLX_LOADX(Q_IN ## 1,PTR_IN,1 * IN_STEP * MXU_A_SIZE);	\
	  CPLX_LOADX(Q_IN ## 2,PTR_IN,2 * IN_STEP * MXU_A_SIZE);	\
	  CPLX_LOADX(Q_IN ## 3,PTR_IN,3 * IN_STEP * MXU_A_SIZE);	\
	}while(0)

#define NE10_RADIX4x4_CPLX_STOREX(PTR_OUT,Q_OUT,OUT_STEP)		\
	  do {								\
	    CPLX_STORE(Q_OUT ## 0,PTR_OUT,0);				\
	    CPLX_STOREX(Q_OUT ## 1,PTR_OUT,1 * OUT_STEP * MXU_A_SIZE);	\
	    CPLX_STOREX(Q_OUT ## 2,PTR_OUT,2 * OUT_STEP * MXU_A_SIZE);	\
	    CPLX_STOREX(Q_OUT ## 3,PTR_OUT,3 * OUT_STEP * MXU_A_SIZE);	\
	  }while(0)


#define NE10_RADIX4x4_CPLX_TRANSPOSE(Q_OUT,DIR)	\
	  do{					\
	    CPLX_TRANSPOSE(Q_OUT ## 0,DIR);	\
	    CPLX_TRANSPOSE(Q_OUT ## 1,DIR);	\
	    CPLX_TRANSPOSE(Q_OUT ## 2,DIR);	\
	    CPLX_TRANSPOSE(Q_OUT ## 3,DIR);	\
	  }while(0)

#define NE10_RADIX4x4_CPLX_TW_TRANSPOSE(Q_TW)	\
	  do{					\
	    CPLX_TRANSPOSE(Q_TW ## 0,0);	\
	    CPLX_TRANSPOSE(Q_TW ## 1,0);	\
	    CPLX_TRANSPOSE(Q_TW ## 2,0);	\
	  }while(0)

#define NE10_RADIX4x4_CPLX_TW_FILL(tw,Q_TW)		\
	  do{						\
	    CPLX_FILL(Q_TW ## 0,tw[0].r,tw[0].i);	\
	    CPLX_FILL(Q_TW ## 1,tw[1].r,tw[1].i);	\
	    CPLX_FILL(Q_TW ## 2,tw[2].r,tw[2].i);	\
	  }while(0)

#define NE10_RADIX4x4_R2C_TW_MUL_MXU(Q2_OUT,Q2_IN,Q2_TW)	\
	  do {							\
	    Q2_OUT ## 0 = Q2_IN ## 0;				\
	    CPLX_MUL(Q2_OUT ## 1,Q2_IN ## 1,Q2_TW ## 0);	\
	    CPLX_MUL(Q2_OUT ## 2,Q2_IN ## 2,Q2_TW ## 1);	\
	    CPLX_MUL(Q2_OUT ## 3,Q2_IN ## 3,Q2_TW ## 2);	\
	  } while(0);


#define NE10_RADIX4x4_R2C_TW_MXU_KERNEL_S1(Q2_OUT,Q2_IN)	\
	  do {							\
	    CPLX_ADD(Q2_OUT ## 0,Q2_IN ## 0,Q2_IN ## 2);	\
	    CPLX_SUB(Q2_OUT ## 1,Q2_IN ## 0,Q2_IN ## 2);	\
	    CPLX_ADD(Q2_OUT ## 2,Q2_IN ## 1,Q2_IN ## 3);	\
	    CPLX_SUB(Q2_OUT ## 3,Q2_IN ## 1,Q2_IN ## 3);	\
	  } while(0);


#define NE10_RADIX4x4_R2C_TW_MXU_KERNEL_S2(Q2_OUT,Q2_IN)		\
	  do {								\
	    Q2_OUT ## 0 .val[0] = Q2_IN ## 0 .val[0] + Q2_IN ## 2 .val[0]; \
	    Q2_OUT ## 0 .val[1] = Q2_IN ## 0 .val[1] + Q2_IN ## 2 .val[1]; \
	    Q2_OUT ## 2 .val[0] = Q2_IN ## 0 .val[0] - Q2_IN ## 2 .val[0]; \
	    Q2_OUT ## 2 .val[1] = Q2_IN ## 2 .val[1] - Q2_IN ## 0 .val[1]; \
	    Q2_OUT ## 1 .val[0] = Q2_IN ## 1 .val[0] + Q2_IN ## 3 .val[1]; \
	    Q2_OUT ## 1 .val[1] = Q2_IN ## 1 .val[1] - Q2_IN ## 3 .val[0]; \
	    Q2_OUT ## 3 .val[0] = Q2_IN ## 1 .val[0] - Q2_IN ## 3 .val[1]; \
	    Q2_OUT ## 3 .val[1] = Q2_IN ## 3 .val[0] + Q2_IN ## 1 .val[1]; \
	    Q2_OUT ## 3 .val[1] = -(Q2_OUT ## 3 .val[1]);		\
	  } while(0);


#define NE10_RADIX4x4_C2R_TW_MUL_MXU(Q2_OUT,Q2_IN,Q2_TW)	\
	  do {							\
	      Q2_OUT ## 0 = Q2_IN ## 0;				\
	      CPLX_INV_MUL(Q2_OUT ## 1,Q2_IN ## 1,Q2_TW ## 0);	\
	      CPLX_INV_MUL(Q2_OUT ## 2,Q2_IN ## 2,Q2_TW ## 1);	\
	      CPLX_INV_MUL(Q2_OUT ## 3,Q2_IN ## 3,Q2_TW ## 2);	\
	      } while(0);



#define NE10_RADIX4x4_C2R_TW_MXU_KERNEL_S1(Q2_OUT,Q2_IN)		\
	  do {								\
	      Q2_IN ## 3 .val[1] = -Q2_IN ## 3 .val[1];			\
	      Q2_OUT ## 0 .val[0] = Q2_IN ## 0 .val[0] + Q2_IN ## 2 .val[0]; \
	      Q2_OUT ## 0 .val[1] = Q2_IN ## 0 .val[1] - Q2_IN ## 2 .val[1]; \
	      Q2_OUT ## 2 .val[0] = Q2_IN ## 0 .val[0] - Q2_IN ## 2 .val[0]; \
	      Q2_OUT ## 2 .val[1] = Q2_IN ## 2 .val[1] + Q2_IN ## 0 .val[1]; \
	      Q2_OUT ## 1 .val[0] = Q2_IN ## 1 .val[0] + Q2_IN ## 3 .val[0]; \
	      Q2_OUT ## 1 .val[1] = Q2_IN ## 1 .val[1] + Q2_IN ## 3 .val[1]; \
	      Q2_OUT ## 3 .val[0] = Q2_IN ## 3 .val[1] - Q2_IN ## 1 .val[1]; \
	      Q2_OUT ## 3 .val[1] = Q2_IN ## 1 .val[0] - Q2_IN ## 3 .val[0]; \
	      } while(0);

#define NE10_RADIX4x4_C2R_TW_MXU_KERNEL_S2(Q2_OUT,Q2_IN)	\
	  do {							\
	    CPLX_ADD(Q2_OUT ## 0,Q2_IN ## 0,Q2_IN ## 1);	\
	    CPLX_SUB(Q2_OUT ## 2,Q2_IN ## 0,Q2_IN ## 1);	\
	    CPLX_ADD(Q2_OUT ## 1,Q2_IN ## 2,Q2_IN ## 3);	\
	    CPLX_SUB(Q2_OUT ## 3,Q2_IN ## 2,Q2_IN ## 3);	\
	  } while(0);


#define NE10_RADIX4x4_C2R_TW_MXU_KERNEL(Q2_OUT,Q2_IN,Q2_TW)	\
	  do {							\
	    NE10_RADIX4x4_C2R_TW_MXU_KERNEL_S1(Q2_OUT,Q2_IN);	\
	    NE10_RADIX4x4_C2R_TW_MXU_KERNEL_S2(Q2_IN,Q2_OUT);	\
	    NE10_RADIX4x4_C2R_TW_MUL_MXU(Q2_OUT,Q2_IN,Q2_TW);	\
	  } while(0);

#define NE10_RADIX4x4_R2C_TW_MXU_KERNEL_LAST(Q_OUT,Q_IN)	\
  do {								\
    v4f32 Q_TMP;						\
    const v4f32 Q_TW_81    = FILL_F(CONST_TW_81 );		\
    Q_IN ## 1 = Q_IN ## 1 * Q_TW_81;				\
    Q_IN ## 3 = Q_IN ## 3 * Q_TW_81;				\
    Q_TMP = Q_IN ## 1 - Q_IN ## 3;				\
    Q_IN ## 3 = Q_IN ## 1 + Q_IN ## 3;				\
    Q_IN ## 1 = Q_TMP;						\
    Q_OUT ## 0 = Q_IN ## 0 + Q_IN ## 1;				\
    Q_OUT ## 1 = Q_IN ## 2 + Q_IN ## 3;				\
    Q_OUT ## 2 = Q_IN ## 0 - Q_IN ## 1;				\
    Q_OUT ## 3 = Q_IN ## 2 - Q_IN ## 3;				\
    Q_OUT ## 1 = -Q_OUT ## 1;					\
  } while(0);

#define NE10_RADIX4x4_C2R_TW_MXU_KERNEL_LAST(Q_OUT,Q_IN)	\
  do {								\
    v4f32 Q_TMP;						\
    const v4f32 DIV_TW81_MXU = FILL_F(DIV_TW81 );		\
    Q_IN ## 1 = - Q_IN ## 1;					\
    Q_OUT ## 0 = Q_IN ## 0 + Q_IN ## 2;				\
    Q_OUT ## 1 = Q_IN ## 0 - Q_IN ## 2;				\
    Q_OUT ## 2 = Q_IN ## 1 + Q_IN ## 3;				\
    Q_OUT ## 3 = Q_IN ## 1 - Q_IN ## 3;				\
    Q_TMP = Q_OUT ## 1 + Q_OUT ## 3;				\
    Q_OUT ## 3 = Q_OUT ## 3 - Q_OUT ## 1;			\
    Q_OUT ## 1 = Q_TMP;						\
    Q_OUT ## 1 = Q_OUT ## 1 * DIV_TW81_MXU;			\
    Q_OUT ## 3 = Q_OUT ## 3 * DIV_TW81_MXU;			\
    Q_OUT ## 0 = Q_OUT ## 0 + Q_OUT ## 0;			\
    Q_OUT ## 2 = Q_OUT ## 2 + Q_OUT ## 2;			\
  } while(0);

#endif // __TRANS_MXU2MSA_H__
