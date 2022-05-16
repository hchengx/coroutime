#include "co.h"
#include <assert.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define STACK_SIZE (64 * 1024)

#ifdef LOCAL_MACHINE
#define debug(...) printf(__VA_ARGS__)
#else
#define debug(...)
#endif

enum co_status
{
    CO_NEW = 1,
    CO_RUNNING,
    CO_WAITING,
    CO_DEAD,
};

struct co
{
    const char* name;
    void (*func)(void*);
    void* arg;

    enum co_status status;
    struct co* waiter;
    jmp_buf context;
    uint8_t stack[STACK_SIZE];
    struct co* next;
    struct co* prev;
};

static struct co* current = NULL;
static struct co* list = NULL;
static struct co* main_co = NULL;

static inline void* co_wrapper(struct co* co)
{
    co->status = CO_RUNNING;

    co->func(co->arg);
    co->status = CO_DEAD;
    co_yield();
    return 0;
}

static inline void stack_switch_call(void* sp, void* entry, void* arg)
{
    asm volatile(
#if __x86_64__
        "movq %0, %%rsp; movq %2, %%rdi; jmp *%1"
        :
        : "b"((uintptr_t)sp - 8), "d"(entry), "a"(arg)
#else
        "movl %0, %%esp; movl %2, 4(%0); jmp *%1"
        :
        : "b"((uintptr_t)sp - 16), "d"(entry), "a"(arg)
#endif
    );
}

struct co* co_start(const char* name, void (*func)(void*), void* arg)
{
    struct co* c = (struct co*)malloc(sizeof(struct co));
    c->name = name;
    c->func = func;
    c->arg = arg;
    c->status = CO_NEW;
    if (list == NULL)
    {
        list = c;
        list->next = list;
        list->prev = list;
    }
    else
    {
        struct co* prev = list->prev;
        prev->next = c;
        c->prev = prev;

        c->next = list;
        list->prev = c;
    }
    return c;
}

static struct co* co_choose()
{
    assert(list != NULL);
    if (current == main_co)
        current = list->prev;
    struct co* temp = current->next;
    while (temp != current)
    {
        if (temp->status == CO_RUNNING || temp->status == CO_NEW)
            break;
        temp = temp->next;
    }
    return temp == current ? main_co : temp;
}

void co_yield()
{
    int status = setjmp(current->context); // 保存上下文
    if (!status)
    {
        current = co_choose();
        if (current->status == CO_NEW)
        {
            ((struct co volatile*)current)->status = CO_RUNNING;
            stack_switch_call(current->stack + STACK_SIZE, co_wrapper, current);
        }
        longjmp(current->context, 1); // 切换上下文
    }
}

// 暂停当前协程，等待 co 完成
void co_wait(struct co* c)
{
    if (c->status != CO_DEAD)
    {
        co_yield();
    }
}

static __attribute__((constructor)) void co_constructor()
{
    main_co = (struct co*)malloc(sizeof(struct co));
    memset(main_co, 0, sizeof(struct co));
    main_co->name = "main";
    main_co->status = CO_RUNNING;
    current = main_co;
}

static __attribute__((destructor)) void co_destructor()
{
    free(main_co);
    while (list && list->next != list)
    {
        struct co* next = list->next;
        list->next = list->next->next;
        free(next);
        list = list->next;
    }
    if (list)
        free(list);
    return;
}