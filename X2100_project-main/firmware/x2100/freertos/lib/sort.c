#include <sort.h>
#include <common.h>
#include <kernel_symbol.h>

#ifdef CONFIG_SORT_UP_CHAR
void sort_up_char(char *data, int size)
{
    int k = 0;
    int i, j, tmp;

    for (i = 0; i < size - 1; i++) {
        tmp = data[i];

        for (j = i + 1; j < size; j++) {
            if (tmp > data[j]) {
                tmp = data[j];
                k = j;
            }
        }
        if (tmp != data[i]) {
            data[k] = data[i];
            data[i] = tmp;
        }
    }
}

EXPORT_SYMBOL(sort_up_char);
#endif


#ifdef CONFIG_SORT_UP_INT
void sort_up_int(int *data, int size)
{
    int k = 0;
    int i, j, tmp;

    for (i = 0; i < size - 1; i++) {
        tmp = data[i];

        for (j = i + 1; j < size; j++) {
            if (tmp > data[j]) {
                tmp = data[j];
                k = j;
            }
        }
        if (tmp != data[i]) {
            data[k] = data[i];
            data[i] = tmp;
        }
    }
}

EXPORT_SYMBOL(sort_up_int);
#endif


#ifdef CONFIG_SORT_DOWN_CHAR
void sort_down_char(char *data, int size)
{
    int k = 0;
    int i, j, tmp;


    for (i = 0; i < size - 1; i++) {
        tmp = data[i];

        for (j = i + 1; j < size; j++) {
            if (tmp < data[j]) {
                tmp = data[j];
                k = j;
            }
        }
        if (tmp != data[i]) {
            data[k] = data[i];
            data[i] = tmp;
        }
    }
}

EXPORT_SYMBOL(sort_down_char);
#endif


#ifdef CONFIG_SORT_DOWN_INT
void sort_down_int(int *data, int size)
{
    int k = 0;
    int i, j, tmp;

    for (i = 0; i < size - 1; i++) {
        tmp = data[i];

        for (j = i + 1; j < size; j++) {
            if (tmp < data[j]) {
                tmp = data[j];
                k = j;
            }
        }
        if (tmp != data[i]) {
            data[k] = data[i];
            data[i] = tmp;
        }
    }
}

EXPORT_SYMBOL(sort_down_int);
#endif




