void clear_bss(int *bss, int *bss_end)
{
    for (;bss < bss_end; bss++) {
        bss[0] = 0;
    }
}