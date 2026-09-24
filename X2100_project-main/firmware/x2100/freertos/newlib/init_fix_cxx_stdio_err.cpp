#include <stdio.h>

class flush_stdio
{
private:
    /* data */
public:
    flush_stdio(/* args */)
    {
        fflush(stdin);
        fflush(stdout);
        fflush(stderr);
    }
};

/*
 * newlib 的 stdout 可能是每个线程一个
 * 然而 c++ 的 std::cout 这些变量会在初始化时引用到第一个线程的 stdout 指针
 * stdout 指向的内容是根据当前线程动态初始化的,所以必须在第一个线程就初始化它的stdout
 * 不然在其它线程执行动态初始化 stdout 的逻辑会有问题
 */
static flush_stdio init_to_fix_cxx_stdio_err;
