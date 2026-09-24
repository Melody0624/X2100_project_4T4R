#ifndef __SPL_CMDARGS_ANALYSIS_H__
#define __SPL_CMDARGS_ANALYSIS_H__

void cmdargs_mem_info_anlysis(void *arg);
int cmdargs_mem_info_get(const char *mem_str, void **start, unsigned int *size);
const char *cmdargs_get(void);

#endif /*  __SPL_CMDARGS_ANALYSIS_H__ */