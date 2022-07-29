
#include <stdio.h>

#include "lib1.h"
#include "lib2.h"
#include "symbolmatch.h"

typedef void(*LIB_PRINT)();

int main()
{
    /* 两个库都调用一次,防止被优化而没有链接库 */
    lib1_init();
    lib2_init();

    /* 第一次调用,无法区分是谁的函数 */
    libx_print();

    LIB_PRINT lib1_print = (LIB_PRINT)sm_getFunAddr("libx_print", "lib1.so");
    LIB_PRINT lib2_print = (LIB_PRINT)sm_getFunAddr("libx_print", "lib2.so");

    /* 第二次调用,区分来自不同动态库的函数 */
    if (lib1_print)
        lib1_print();
    if (lib2_print)
        lib2_print();

    return 0;
}
