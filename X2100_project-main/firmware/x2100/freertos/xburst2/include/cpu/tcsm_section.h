#ifndef _TCSM_SECTION_H_
#define _TCSM_SECTION_H_

/*
 * 使用 __tcsm_section 将函数放入 tcsm 中时
 * 尽量使用 tcsm_call 调用其它不在tscm 中的函数
 * 否则可能出现调用异常或者编译不过的问题
 */

#ifdef CONFIG_TCSM_SECTION

#define tcsm_call(func,...) \
({ \
    register typeof(func) * volatile _func = (func); \
    _func(__VA_ARGS__); \
})

#define __tcsm_section __attribute__((section(".tcsm_func")))

#else

#define tcsm_call(func, x...) func(x)

#define __tcsm_section

#endif

void soc_tcsm_section_init(void);

#endif /* _TCSM_SECTION_H_ */
