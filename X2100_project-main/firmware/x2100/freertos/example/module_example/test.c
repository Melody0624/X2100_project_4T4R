extern int printf(const char * fmt, ...);

int math_add(int a, int b)
{
    printf("funticon call math:add\n");
    printf("module_a = %d\n", a);
    printf("module_b = %d\n", b);

    return a + b;
}

int math_sub(int a, int b)
{
    printf("funticon call math:sub\n");
    printf("module_a = %d\n", a);
    printf("module_b = %d\n", b);

    return a - b;
}
