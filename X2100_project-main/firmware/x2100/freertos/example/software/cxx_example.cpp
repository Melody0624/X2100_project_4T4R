#include <stdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <vector>
// #include <iostream>

#ifndef CONFIG_OS_HAS_CPP_SUPPORT
#error "select your c++ support lib"
#endif

/*
 * 包含c 语言定义的头文件
 */
extern "C" {
#include <common.h>
};

class parent {
public:
    parent() {
        printf("init parent\n");
    }
    virtual void virt_func() {};

    static void test()
    {

    }
};

class test : public parent {
public:
    test() {
        printf("init test\n");
    }

    void virt_func() {
        printf("virt in test\n");
    }
};

/*
 * c 语言可以调用 cxx_test()
 */
extern "C" void cxx_test();

void cxx_test(void)
{
    test t1;
    t1.virt_func();

    parent *p = new test();

    p->virt_func();
    p->test();
    printf("p->test: %p\n", p->test);

    std::vector<int> a;
    a.push_back(1);
    printf("a.at(0) %d\n", a.at(0));
    a.pop_back();
    printf("a.size() %d\n", a.size());

    /*
     * libstdc++ 没有iostream的实现
     */
#if 0
    std::cout << "hello world" << std::endl;
#endif
}
