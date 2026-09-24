#ifndef _MIPS_REGS_OTHERS_H_
#define _MIPS_REGS_OTHERS_H_

#include "bits_opt.h"

#define CAUSEB_DC              27
#define CAUSEF_DC              (_ULCAST_(1)   << 27)

#define ECCB_WST               29
#define ECCF_WST               (_ULCAST_(1)   << 29)

#define STATUSB_CU1            29
#define STATUSF_CU1            (_ULCAST_(1)   << 29)

#define STATUSB_CU2            30
#define STATUSF_CU2            (_ULCAST_(1)   << 30)

#define EBASEB_WG              11
#define EBASEF_WG              (_ULCAST_(1)   << 11)

#define EBASE_CPUNum           0, 9

#define CONFIG_K0              0, 2

#define CONFIG5_MSAEn           27, 27
#define CONFIG5B_MSAEn          27
#define CONFIG5F_MSAEn          (_ULCAST_(1)   << 27)

#define MSA_CSR    $1

#endif /* _MIPS_REGS_OTHERS_H_ */