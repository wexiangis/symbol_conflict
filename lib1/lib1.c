#include <stdio.h>

void lib1_init()
{
    printf("%s \r\n", __FUNCTION__);
}

void libx_print()
{
    printf("%s : frome lib1 \r\n", __FUNCTION__);
}
