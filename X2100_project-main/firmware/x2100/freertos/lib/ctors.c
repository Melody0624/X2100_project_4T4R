
extern void (*__ctors_start[])(void);
extern void (*__ctors_end[])(void);

void init_ctors(void)
{
    int count = __ctors_end - __ctors_start;
    int i;

    for (i = 0; i < count; i++) {
        __ctors_start[i]();
    }
}
