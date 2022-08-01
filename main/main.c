
#include <stdio.h>

#include "lib1.h"
#include "lib2.h"
#include "symbolmatch.h"

int main()
{

    /* 两个库都调用一次,防止被优化而没有链接库 */
    lib1_init();
    lib2_init();

    /* 第一次调用,无法区分是谁的函数 */
    libx_print();

    /* 第二次调用,区分来自不同动态库的函数 */
    void(*lib1_print)() = (void(*)())sm_getFunAddr("libx_print", "lib1.so");
    void(*lib2_print)() = (void(*)())sm_getFunAddr("libx_print", "lib2.so");

    lib1_print();
    lib2_print();

    return 0;
}
