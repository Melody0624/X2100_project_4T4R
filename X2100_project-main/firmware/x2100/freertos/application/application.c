#include <stdio.h>

#ifdef CONFIG_APPLICATION_LOAD_KERNEL
void application_load_kernel(void *arg);
#endif

#ifdef  CONFIG_APPLICATION_FACE
void application_face(void);
#endif

void applicetion_init(void *arg)
{
#ifdef CONFIG_APPLICATION_LOAD_KERNEL
    application_load_kernel(arg);
#endif

#ifdef  CONFIG_APPLICATION_FACE
    application_face();
#endif
}
