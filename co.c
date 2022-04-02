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

static inline void stack_switch_call(void* sp, void* entry, void* arg)
{
    void* sp_ = NULL;
    void* sp1_ = NULL;
    void* sb_ = NULL;
    void* sb1_ = NULL;

    __asm__ volatile("movq %%rsp, %0\n\t"
                     "movq %%rbp, %1"
                     : "=r"(sp_), "=r"(sb_)
                     :);

    __asm__ volatile("movq %0, %%rsp\n\t"
                     "movq %2, %%rdi\n\t"
                     "callq *%1"
                     :
                     : "b"((uintptr_t)sp - 32), "d"((uintptr_t)entry),
                       "a"((uintptr_t)arg));
    __asm__ volatile("movq %0, %%rsp" : : "r"((uintptr_t)sp_));

    __asm__ volatile("movq %%rsp, %0\n\t"
                     "movq %%rbp, %1"
                     : "=r"(sp1_), "=r"(sb1_)
                     :);
    assert(sp_ == sp1_ && sb_ == sb1_);
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
    int status = setjmp(current->context);
    if (!status)
    {
        current = co_choose();
        if (current->status == CO_RUNNING)
            longjmp(current->context, 1);
        else
        {
            ((struct co volatile*)current)->status = CO_RUNNING;
            stack_switch_call(current->stack + STACK_SIZE, current->func,
                              current->arg);
            current->status = CO_DEAD;
            co_yield();
        }
    }
}

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