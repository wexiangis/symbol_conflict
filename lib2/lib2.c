#include <stdio.h>

void lib2_init()
{
    printf("%s \r\n", __FUNCTION__);
}

void libx_print()
{
    printf("%s : frome lib2 \r\n", __FUNCTION__);
}
