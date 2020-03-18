#pragma once

#define GC_THREADS
#include <gc.h>

#include "natalie.h"

void nat_gc_init();
NatObject *nat_alloc(NatEnv *env);

#define nat_malloc_root(size) GC_MALLOC(size)
#define nat_malloc(env, size) GC_MALLOC(size)
#define nat_realloc_root(ptr, size) GC_REALLOC(ptr, size)
#define nat_realloc(env, ptr, size) GC_REALLOC(ptr, size)

#define nat_pthread_create(thread, attr, fn, arg) GC_pthread_create(thread, attr, fn, arg)
#define nat_pthread_join(thread, value_ptr) GC_pthread_join(thread, value_ptr)
