#pragma once

#include "natalie.h"

#define NAT_BLOCK_SIZE 16384 // 16 KiB

void nat_gc_init();
NatObject *nat_alloc(NatEnv *env);
void *nat_malloc_root(size_t size);
void *nat_malloc(NatEnv *env, size_t size);
void *nat_realloc_root(void *ptr, size_t size);
void *nat_realloc(NatEnv *env, void *ptr, size_t size);
