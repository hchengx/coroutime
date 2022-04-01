#include "../co.h"
#include <stdio.h>

void func(void* arg)
{
    char a = (char)arg;
    for (int i = 0; i < 10; i++)
    {
        printf("%c: i = %d\n", a, i);
        co_yield();
    }
}

int main(int argc, char* argv[])
{
    struct co* co1 = co_start("func1", func, (void*)'a');
    struct co* co2 = co_start("func2", func, (void*)'b');
    co_wait(co1);
    co_wait(co2);
    co_wait(co1);
    co_wait(co2);
    return 0;
}