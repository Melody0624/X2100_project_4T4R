#ifndef X2100_CALIB_CONFIGS_H
#define X2100_CALIB_CONFIGS_H

#include "wave2regs_intf.h"
#include "cheetah.h"

extern struct reg_line X2100_first_calib_regs[];
extern struct reg_line X2100_second_calib_regs[];

extern const ParamsRegsConfig X2100_first_phaseCalib_default_config;
extern const ParamsRegsConfig X2100_second_phaseCalib_default_config;

extern struct reg_line X2100_Reset_cheetah_default_config[];
extern const ParamsRegsConfig X2100_reset_phaseCalib_default_config;

#endif // X2100_CALIB_CONFIGS_H
