#include <stdio.h>
#include <string.h>
#include "module.h"
#include <kernel_param.h>
#include <module_param.h>

/*
 * 加载模块
 * insmod module_example.mo
 * insmod module_example.mo module_a=456 module_b=123
 * insmod module_example.mo name=Hello para=11,22,33,44,55,66,77,88
 *
 * 卸载模块
 * rmmod module_example
 */
int a;
int b = 5;

extern int *ap;
extern int *bp;

static unsigned int module_default_a = 1;
module_param_named(module_a, module_default_a, int, 0644);

static unsigned int module_default_b = 2;
module_param_named(module_b, module_default_b, int, 0644);

static int para[8] = {1,2,3,4};
static int n_para = 4;
module_param_array(para , int , &n_para , 0644);

//static char name[16] = "Ingenic";
//module_param_string(name, name, 16, 0644);

static char *name = "Ingenic";
module_param(name, charp, 0644);

extern int printf(const char * fmt, ...);
extern int math_add(int a, int b);
extern int math_sub(int a, int b);


int moduel_test_init(void)
{
    int i = 0;
    printf("this is from relocate code\n");

    printf("a: %d &a:%p\n", a, &a);
    printf("printf:%p\n", printf);
    printf("a0:%d b:%d\n", a, b);
    printf("\n");

    printf("name:%s\n", name);
    printf("Array Number=%d\n", n_para);
    for (i=0; i<n_para; i++) {
        printf("array[%d]=%d\n", i, para[i]);
    }
    printf("\n");

    printf("Function call\n");
    printf("%d + %d = %d\n", module_default_a, module_default_b,
                             math_add(module_default_a, module_default_b) );
    printf("%d - %d = %d\n", module_default_a, module_default_b,
                             math_sub(module_default_a, module_default_b) );

    return 0;
}

void moduel_test_exit(void)
{
    printf("module_test_exit\n");
}


module_exit(moduel_test_exit);
module_init(moduel_test_init);

