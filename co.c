#include "co.h"
#include <assert.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static inline void stack_switch_call(void* sp, void* entry, void* arg)
{
    __asm__ volatile(
#if __x86_64__
        "movq %%rcx, 0(%0); movq %0, %%rsp; movq %2, %%rdi; call *%1"
        :
        : "b"((uintptr_t)sp - 16), "d"((uintptr_t)entry), "a"((uintptr_t)arg)
#else
        "movl %%ecx, 4(%0); movl %0, %%esp; movl %2, 0(%0); call *%1"
        :
        : "b"((uintptr_t)sp - 8), "d"((uintptr_t)entry), "a"((uintptr_t)arg)
#endif
    );
}

static inline void restore_return()
{
    __asm__ volatile(
#if __x86_64__
        "movq 0(%%rsp), %%rcx"
        :
        :
#else
        "movl 4(%%esp), %%ecx"
        :
        :
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

static struct co* co_choose(struct co* current)
{
    struct co* temp = current->next;
    while (temp != current)
    {
        if (temp->status == CO_RUNNING || temp->status == CO_NEW)
            break;
        temp = temp->next;
    }
    return temp;
}

void co_yield()
{
    int status = setjmp(current->context);
    if (!status)
    {
        current = co_choose(current);
        if (current->status == CO_RUNNING)
            longjmp(current->context, 1);
        else
        {
            ((struct co volatile*)current)->status = CO_RUNNING;
            stack_switch_call(current->stack + STACK_SIZE, current->func,
                              current->arg);
            restore_return();
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
    current = co_start("main", NULL, NULL);
    current->status = CO_RUNNING;
}

static __attribute__((destructor)) void co_destructor()
{
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