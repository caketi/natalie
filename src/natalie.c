#include "natalie.h"
#include <ctype.h>
#include <stdarg.h>
#include <gc.h>
#include <math.h>

int nat_is_constant_name(char *name) {
    return strlen(name) > 0 && isupper(name[0]);
}

int nat_is_global_name(char *name) {
    return strlen(name) > 0 && name[0] == '$';
}

NatObject *nat_const_get(NatEnv *env, NatObject *klass, char *name) {
    NatObject *val = nat_const_get_or_null(env, klass, name);
    if (val) {
        return val;
    } else {
        NAT_RAISE(env, nat_const_get(env, Object, "NameError"), "uninitialized constant %s", name);
    }
}

NatObject *nat_const_get_or_null(NatEnv *env, NatObject *klass, char *name) {
    if (NAT_TYPE(klass) != NAT_VALUE_CLASS) {
        klass = NAT_OBJ_CLASS(klass);
    }
    NatObject *val;
    while (!(val = hashmap_get(&klass->constants, name)) && klass->superclass) {
        klass = klass->superclass;
    }
    if (val) {
        return val;
    } else {
        return NULL;
    }
}

NatObject *nat_const_set(NatEnv *env, NatObject *klass, char *name, NatObject *val) {
    if (NAT_TYPE(klass) != NAT_VALUE_CLASS) {
        klass = klass->klass;
    }
    hashmap_remove(&klass->constants, name);
    hashmap_put(&klass->constants, name, val);
    return val;
}

NatObject *nat_var_get2(NatEnv *env, char *key, size_t index) {
    if (index >= env->var_count) {
        printf("Trying to get variable `%s' at index %zu which is not set (env->vars = %p, env->var_count = %zu).\n", key, index, env->vars, env->var_count);
        abort();
    }
    return env->vars[index];
}

NatObject *nat_var_set2(NatEnv *env, char *key, size_t index, NatObject *val) {
    if (env->var_count == 0) {
        env->vars = GC_MALLOC((index + 1) * sizeof(NatObject*));
        env->var_count = index + 1;
    } else if (index >= env->var_count) {
        env->vars = GC_REALLOC(env->vars, sizeof(NatObject*) * index + 1);
        env->var_count = index + 1;
    }
    env->vars[index] = val;
    return val;
}

NatEnv *nat_build_env(NatEnv *outer) {
    NatEnv *env = GC_MALLOC(sizeof(NatEnv));
    env->var_count = 0;
    env->vars = NULL;
    env->block = FALSE;
    env->outer = outer;
    env->jump_buf = NULL;
    env->caller = NULL;
    env->line = 0;
    env->method_name = NULL;
    if (outer) {
        env->global_env = outer->global_env;
    } else {
        NatGlobalEnv *global_env = GC_MALLOC(sizeof(NatGlobalEnv));
        struct hashmap *global_table = GC_MALLOC(sizeof(struct hashmap));
        hashmap_init(global_table, hashmap_hash_string, hashmap_compare_string, 100);
        hashmap_set_key_alloc_funcs(global_table, hashmap_alloc_key_string, NULL);
        global_env->globals = global_table;
        struct hashmap *symbol_table = GC_MALLOC(sizeof(struct hashmap));
        hashmap_init(symbol_table, hashmap_hash_string, hashmap_compare_string, 100);
        hashmap_set_key_alloc_funcs(symbol_table, hashmap_alloc_key_string, NULL);
        global_env->symbols = symbol_table;
        global_env->next_object_id = GC_MALLOC_ATOMIC(sizeof(uint64_t));
        *global_env->next_object_id = 1;
        env->global_env = global_env;
    }
    return env;
}

NatEnv *nat_build_block_env(NatEnv *outer, NatEnv *calling_env) {
    NatEnv *env = nat_build_env(outer);
    env->block = TRUE;
    env->caller = calling_env;
    return env;
}

char *nat_find_current_method_name(NatEnv *env) {
    while ((!env->method_name || strcmp(env->method_name, "<block>") == 0) && env->outer) {
        env = env->outer;
    }
    if (strcmp(env->method_name, "<main>") == 0) return NULL;
    return env->method_name;
}

char *nat_find_method_name(NatEnv *env) {
    while (1) {
        if (env->method_name) {
            if (strcmp(env->method_name, "<block>") == 0) {
                return nat_sprintf(env, "block in %s", nat_find_method_name(env->outer))->str;
            } else {
                return env->method_name;
            }
        }
        if (!env->outer) break;
        env = env->outer;
    }
    return heap_string("(unknown)");
}

NatObject* nat_raise(NatEnv *env, NatObject *klass, char *message_format, ...) {
    va_list args;
    va_start(args, message_format);
    NatObject *message = nat_vsprintf(env, message_format, args);
    va_end(args);
    NatObject *exception = nat_exception(env, klass, message->str);
    nat_raise_exception(env, exception);
    return exception;
}

NatObject* nat_raise_exception(NatEnv *env, NatObject *exception) {
    NatObject *bt = exception->backtrace = nat_array(env);
    NatEnv *bt_env = env;
    while (1) {
        if (bt_env->file) {
            char *method_name = nat_find_method_name(bt_env);
            nat_array_push(bt, nat_sprintf(env, "%s:%d:in `%s'", bt_env->file, bt_env->line, method_name));
        }
        if (!bt_env->caller) break;
        bt_env = bt_env->caller;
    }
    int counter = 0;
    while (env && !env->jump_buf) {
        assert(++counter < 10000); // serious problem

        if (env->caller) {
            env = env->caller;
        } else {
            env = env->outer;
        }
    }
    assert(env);
    env->exception = exception;
    longjmp(*env->jump_buf, 1);
}

NatObject *nat_ivar_get(NatEnv *env, NatObject *obj, char *name) {
    assert(strlen(name) > 0);
    if(name[0] != '@') {
        NAT_RAISE(env, nat_const_get(env, Object, "NameError"), "`%s' is not allowed as an instance variable name", name);
    }
    if (obj->ivars.table == NULL) {
        hashmap_init(&obj->ivars, hashmap_hash_string, hashmap_compare_string, 100);
        hashmap_set_key_alloc_funcs(&obj->ivars, hashmap_alloc_key_string, NULL);
    }
    NatObject *val = hashmap_get(&obj->ivars, name);
    if (val) {
        return val;
    } else {
        return nil;
    }
}

NatObject *nat_ivar_set(NatEnv *env, NatObject *obj, char *name, NatObject *val) {
    assert(strlen(name) > 0);
    if(name[0] != '@') {
        NAT_RAISE(env, nat_const_get(env, Object, "NameError"), "`%s' is not allowed as an instance variable name", name);
    }
    if (obj->ivars.table == NULL) {
        hashmap_init(&obj->ivars, hashmap_hash_string, hashmap_compare_string, 100);
        hashmap_set_key_alloc_funcs(&obj->ivars, hashmap_alloc_key_string, NULL);
    }
    hashmap_remove(&obj->ivars, name);
    hashmap_put(&obj->ivars, name, val);
    return val;
}

NatObject *nat_global_get(NatEnv *env, char *name) {
    assert(strlen(name) > 0);
    if(name[0] != '$') {
        NAT_RAISE(env, nat_const_get(env, Object, "NameError"), "`%s' is not allowed as an global variable name", name);
    }
    NatObject *val = hashmap_get(env->global_env->globals, name);
    if (val) {
        return val;
    } else {
        return nil;
    }
}

NatObject *nat_global_set(NatEnv *env, char *name, NatObject *val) {
    assert(strlen(name) > 0);
    if(name[0] != '$') {
        NAT_RAISE(env, nat_const_get(env, Object, "NameError"), "`%s' is not allowed as an global variable name", name);
    }
    hashmap_remove(env->global_env->globals, name);
    hashmap_put(env->global_env->globals, name, val);
    return val;
}

#define TRUE 1
#define FALSE 0

int nat_truthy(NatObject *obj) {
    if (NAT_TYPE(obj) == NAT_VALUE_FALSE || NAT_TYPE(obj) == NAT_VALUE_NIL) {
        return FALSE;
    } else {
        return TRUE;
    }
}

char *heap_string(char *str) {
    size_t len = strlen(str);
    char *copy = GC_MALLOC_ATOMIC(len + 1);
    memcpy(copy, str, len + 1);
    return copy;
}

NatObject *nat_alloc(NatEnv *env) {
    NatObject *obj = GC_MALLOC(sizeof(NatObject));
    obj->flags = 0;
    obj->type = NAT_VALUE_OTHER;
    obj->env = NULL;
    obj->included_modules_count = 0;
    obj->included_modules = NULL;
    obj->id = nat_next_object_id(env);
    obj->singleton_class = NULL;
    return obj;
}

NatObject *nat_subclass(NatEnv *env, NatObject *superclass, char *name) {
    NatObject *klass = nat_alloc(env);
    klass->type = NAT_VALUE_CLASS;
    klass->klass = superclass->klass;
    if (superclass->singleton_class) {
        klass->singleton_class = nat_subclass(env, superclass->singleton_class, NULL);
    }
    klass->class_name = name ? heap_string(name) : NULL;
    klass->superclass = superclass;
    klass->env = nat_build_env(superclass->env);
    hashmap_init(&klass->methods, hashmap_hash_string, hashmap_compare_string, 10);
    hashmap_set_key_alloc_funcs(&klass->methods, hashmap_alloc_key_string, NULL);
    hashmap_init(&klass->constants, hashmap_hash_string, hashmap_compare_string, 10);
    hashmap_set_key_alloc_funcs(&klass->constants, hashmap_alloc_key_string, NULL);
    return klass;
}

NatObject *nat_module(NatEnv *env, char *name) {
    NatObject *val = nat_alloc(env);
    val->type = NAT_VALUE_MODULE;
    val->klass = nat_const_get(env, Object, "Module");
    val->class_name = name ? heap_string(name) : NULL;
    val->env = nat_build_env(env);
    hashmap_init(&val->methods, hashmap_hash_string, hashmap_compare_string, 100);
    hashmap_set_key_alloc_funcs(&val->methods, hashmap_alloc_key_string, NULL);
    return val;
}

void nat_class_include(NatObject *klass, NatObject *module) {
    klass->included_modules_count++;
    if (klass->included_modules_count == 1) {
        klass->included_modules = GC_MALLOC(sizeof(NatObject*));
    } else {
        klass->included_modules = GC_REALLOC(klass->included_modules, sizeof(NatObject*) * klass->included_modules_count);
    }
    klass->included_modules[klass->included_modules_count - 1] = module;
}

NatObject *nat_new(NatEnv *env, NatObject *klass, size_t argc, NatObject **args, struct hashmap *kwargs, NatBlock *block) {
    NatObject *obj = nat_alloc(env);
    obj->klass = klass;
    while (1) {
        if (hashmap_get(&klass->methods, "initialize")) {
            nat_call_method_on_class(env, klass, klass, "initialize", obj, argc, args, kwargs, block);
        }
        if (!klass->superclass) break;
        klass = klass->superclass;
    }
    return obj;
}

NatObject *nat_singleton_class(NatEnv *env, NatObject *obj) {
    if (!obj->singleton_class) {
        obj->singleton_class = nat_subclass(env, obj->klass, NULL);
    }
    return obj->singleton_class;
}

NatObject *nat_integer(NatEnv *env, int64_t integer) {
    assert(integer >= NAT_MIN_INT);
    assert(integer <= NAT_MAX_INT);
    return (NatObject*)(integer << 1 | 1);
}

NatObject *nat_string(NatEnv *env, char *str) {
    NatObject *obj = nat_new(env, nat_const_get(env, Object, "String"), 0, NULL, NULL, NULL);
    obj->type = NAT_VALUE_STRING;
    size_t len = strlen(str);
    obj->str = heap_string(str);
    obj->str_len = len;
    obj->str_cap = len;
    return obj;
}

NatObject *nat_symbol(NatEnv *env, char *name) {
    NatObject *symbol = hashmap_get(env->global_env->symbols, name);
    if (symbol) {
        return symbol;
    } else {
        symbol = nat_new(env, nat_const_get(env, Object, "Symbol"), 0, NULL, NULL, NULL);
        symbol->type = NAT_VALUE_SYMBOL;
        symbol->symbol = name;
        hashmap_remove(env->global_env->symbols, name);
        hashmap_put(env->global_env->symbols, name, symbol);
        return symbol;
    }
}

NatObject *nat_exception(NatEnv *env, NatObject *klass, char *message) {
    NatObject *obj = nat_new(env, klass, 0, NULL, NULL, NULL);
    obj->type = NAT_VALUE_EXCEPTION;
    obj->message = message;
    return obj;
}

NatObject *nat_array(NatEnv *env) {
    NatObject *obj = nat_new(env, nat_const_get(env, Object, "Array"), 0, NULL, NULL, NULL);
    obj->type = NAT_VALUE_ARRAY;
    obj->ary = GC_MALLOC(NAT_ARRAY_INIT_SIZE * sizeof(NatObject*));
    obj->str_len = 0;
    obj->str_cap = NAT_ARRAY_INIT_SIZE;
    return obj;
}

void nat_grow_array(NatObject *obj, size_t capacity) {
    obj->ary = GC_REALLOC(obj->ary, sizeof(NatObject*) * capacity);
    obj->ary_cap = capacity;
}

void nat_grow_array_at_least(NatObject *obj, size_t min_capacity) {
    size_t capacity = obj->ary_cap;
    if (capacity >= min_capacity)
        return;
    if (capacity > 0 && min_capacity <= capacity * NAT_ARRAY_GROW_FACTOR) {
        nat_grow_array(obj, capacity * NAT_ARRAY_GROW_FACTOR);
    } else {
        nat_grow_array(obj, min_capacity);
    }
}

void nat_array_push(NatObject *array, NatObject *obj) {
    assert(NAT_TYPE(array) == NAT_VALUE_ARRAY);
    size_t capacity = array->ary_cap;
    size_t len = array->ary_len;
    if (len >= capacity) {
        nat_grow_array_at_least(array, len + 1);
    }
    array->ary_len++;
    array->ary[len] = obj;
}

void nat_array_push_splat(NatEnv *env, NatObject *array, NatObject *obj) {
    assert(NAT_TYPE(array) == NAT_VALUE_ARRAY);
    if (NAT_TYPE(obj) != NAT_VALUE_ARRAY && nat_respond_to(env, obj, "to_a")) {
        obj = nat_send(env, obj, "to_a", 0, NULL, NULL);
    }
    if (NAT_TYPE(obj) == NAT_VALUE_ARRAY) {
        for (size_t i=0; i<obj->ary_len; i++) {
            nat_array_push(array, obj->ary[i]);
        }
    } else {
        nat_array_push(array, obj);
    }
}

void nat_array_expand_with_nil(NatEnv *env, NatObject *array, size_t size) {
    assert(NAT_TYPE(array) == NAT_VALUE_ARRAY);
    for (size_t i=array->ary_len; i<size; i++) {
        nat_array_push(array, nil);
    }
}

// this is used by the hashmap library and assumes that obj->env has been set
size_t nat_hashmap_hash(const void *obj) {
    assert(((NatObject*)obj)->env);
    NatObject *hash_obj = nat_send(((NatObject*)obj)->env, (NatObject*)obj, "hash", 0, NULL, NULL);
    assert(NAT_TYPE(hash_obj) == NAT_VALUE_INTEGER);
    return NAT_INT_VALUE(hash_obj);
}

// this is used by the hashmap library to compare keys
int nat_hashmap_compare(const void *a, const void *b) {
    NatObject *a_hash = nat_send(((NatObject*)a)->env, (NatObject*)a, "hash", 0, NULL, NULL);
    NatObject *b_hash = nat_send(((NatObject*)b)->env, (NatObject*)b, "hash", 0, NULL, NULL);
    assert(NAT_TYPE(a_hash) == NAT_VALUE_INTEGER);
    assert(NAT_TYPE(b_hash) == NAT_VALUE_INTEGER);
    return NAT_INT_VALUE(a_hash) - NAT_INT_VALUE(b_hash);
}

NatHashKeyListNode *nat_hash_key_list_append(NatObject *hash, NatObject *key, NatObject *val) {
    if (hash->key_list) {
        NatHashKeyListNode *first = hash->key_list;
        NatHashKeyListNode *last = hash->key_list->prev;
        NatHashKeyListNode *new_last = GC_MALLOC(sizeof(NatHashKeyListNode));
        new_last->key = key;
        new_last->val = val;
        // <first> ... <last> <new_last> -|
        // ^______________________________|
        new_last->prev = last;
        new_last->next = first;
        new_last->removed = FALSE;
        first->prev = new_last;
        last->next = new_last;
        return new_last;
    } else {
        NatHashKeyListNode *node = GC_MALLOC(sizeof(NatHashKeyListNode));
        node->key = key;
        node->val = val;
        node->prev = node;
        node->next = node;
        node->removed = FALSE;
        hash->key_list = node;
        return node;
    }
}

void nat_hash_key_list_remove_node(NatObject *hash, NatHashKeyListNode *node) {
    NatHashKeyListNode *prev = node->prev;
    NatHashKeyListNode *next = node->next;
    // <prev> <-> <node> <-> <next>
    if (node == next) {
        // <node> -|
        // ^_______|
        node->prev = NULL;
        node->next = NULL;
        node->removed = TRUE;
        hash->key_list = NULL;
        return;
    } else if (hash->key_list == node) {
        // starting point is the node to be removed, so shift them forward by one
        hash->key_list = next;
    }
    // remove the node
    node->removed = TRUE;
    prev->next = next;
    next->prev = prev;
}

NatHashIter *nat_hash_iter(NatEnv *env, NatObject *hash) {
    return hash->key_list;
}

NatHashIter *nat_hash_iter_prev(NatEnv *env, NatObject *hash, NatHashIter *iter) {
    if (iter->prev == NULL || iter == hash->key_list) {
        // finished
        return NULL;
    } else if (iter->prev->removed) {
        return nat_hash_iter_prev(env, hash, iter->prev);
    } else {
        return iter->prev;
    }
}

NatHashIter *nat_hash_iter_next(NatEnv *env, NatObject *hash, NatHashIter *iter) {
    if (iter->next == NULL || (!iter->removed && iter->next == hash->key_list)) {
        // finished
        return NULL;
    } else if (iter->next->removed) {
        return nat_hash_iter_next(env, hash, iter->next);
    } else {
        return iter->next;
    }
}

NatObject *nat_hash(NatEnv *env) {
    NatObject *obj = nat_new(env, nat_const_get(env, Object, "Hash"), 0, NULL, NULL, NULL);
    obj->type = NAT_VALUE_HASH;
    obj->key_list = NULL;
    hashmap_init(&obj->hashmap, nat_hashmap_hash, nat_hashmap_compare, 256);
    return obj;
}

// We need an "actual" Integer object to store the env for hashmap keys.
// FIXME: this stinks, rethink this
NatObject *nat_convert_to_real_object(NatEnv *env, NatObject *obj) {
    if (((int64_t)obj & 1)) {
        NatObject *real_obj = nat_new(env, Integer, 0, NULL, NULL, NULL);
        real_obj->type = NAT_VALUE_INTEGER;
        real_obj->integer = NAT_INT_VALUE(obj);
        return real_obj;
    } else {
        return obj;
    }
}

NatObject *nat_hash_get(NatEnv *env, NatObject *hash, NatObject *key) {
    assert(NAT_TYPE(hash) == NAT_VALUE_HASH);
    key = nat_convert_to_real_object(env, key);
    if (!key->env) key->env = env;
    NatHashValueContainer *container = hashmap_get(&hash->hashmap, key);
    if (container) {
        return container->val;
    } else {
        return NULL;
    }
}

void nat_hash_put(NatEnv *env, NatObject *hash, NatObject *key, NatObject *val) {
    assert(NAT_TYPE(hash) == NAT_VALUE_HASH);
    key = nat_convert_to_real_object(env, key);
    // nat_hashmap_hash and nat_hashmap_compare use key->env because we cannot pass it in as an argument
    if (!key->env) key->env = env;
    NatHashValueContainer *container = hashmap_get(&hash->hashmap, key);
    if (container) {
        container->key_list_node->val = val;
        container->val = val;
    } else {
        container = GC_MALLOC(sizeof(NatHashValueContainer));
        container->key_list_node = nat_hash_key_list_append(hash, key, val);
        container->val = val;
        hashmap_put(&hash->hashmap, key, container);
    }
}

NatObject* nat_hash_delete(NatEnv *env, NatObject *hash, NatObject *key) {
    assert(hash->type = NAT_VALUE_HASH);
    key = nat_convert_to_real_object(env, key);
    if (!key->env) key->env = env;
    NatHashValueContainer *container = hashmap_remove(&hash->hashmap, key);
    if (container) {
        nat_hash_key_list_remove_node(hash, container->key_list_node);
        return container->val;
    } else {
        return NULL;
    }
}

#define INT_64_MAX_CHAR_LEN 21 // 1 for sign, 19 for max digits, and 1 for null terminator

char* int_to_string(int64_t num) {
    char* str;
    if (num == 0) {
        return heap_string("0");
    } else {
        str = GC_MALLOC_ATOMIC(INT_64_MAX_CHAR_LEN);
        snprintf(str, INT_64_MAX_CHAR_LEN, "%" PRId64, num);
        return str;
    }
}

void nat_define_method(NatObject *obj, char *name, NatObject* (*fn)(NatEnv*, NatObject*, size_t, NatObject**, struct hashmap*, NatBlock *block)) {
    NatMethod *method = GC_MALLOC(sizeof(NatMethod));
    method->fn = fn;
    method->env = NULL;
    if (nat_is_main_object(obj)) {
        hashmap_remove(&obj->klass->methods, name);
        hashmap_put(&obj->klass->methods, name, method);
    } else {
        hashmap_remove(&obj->methods, name);
        hashmap_put(&obj->methods, name, method);
    }
}

void nat_define_method_with_block(NatObject *obj, char *name, NatBlock *block) {
    NatMethod *method = GC_MALLOC(sizeof(NatMethod));
    method->fn = block->fn;
    method->env = block->env;
    if (nat_is_main_object(obj)) {
        hashmap_remove(&obj->klass->methods, name);
        hashmap_put(&obj->klass->methods, name, method);
    } else {
        hashmap_remove(&obj->methods, name);
        hashmap_put(&obj->methods, name, method);
    }
}

void nat_define_singleton_method(NatEnv *env, NatObject *obj, char *name, NatObject* (*fn)(NatEnv*, NatObject*, size_t, NatObject**, struct hashmap*, NatBlock *block)) {
    NatMethod *method = GC_MALLOC(sizeof(NatMethod));
    method->fn = fn;
    method->env = NULL;
    NatObject *klass = nat_singleton_class(env, obj);
    hashmap_remove(&klass->methods, name);
    hashmap_put(&klass->methods, name, method);
}

NatObject *nat_class_ancestors(NatEnv *env, NatObject *klass) {
    assert(NAT_TYPE(klass) == NAT_VALUE_CLASS || NAT_TYPE(klass) == NAT_VALUE_MODULE);
    NatObject *ancestors = nat_array(env);
    while (1) {
        nat_array_push(ancestors, klass);
        for (size_t i=0; i<klass->included_modules_count; i++) {
            nat_array_push(ancestors, klass->included_modules[i]);
        }
        if (!klass->superclass) break;
        klass = klass->superclass;
    }
    return ancestors;
}

int nat_is_a(NatEnv *env, NatObject *obj, NatObject *klass_or_module) {
    if (obj == klass_or_module) {
        return TRUE;
    } else {
        NatObject *ancestors = nat_class_ancestors(env, NAT_OBJ_CLASS(obj));
        for (size_t i=0; i<ancestors->ary_len; i++) {
            if (klass_or_module == ancestors->ary[i]) {
                return TRUE;
            }
        }
        return FALSE;
    }
}

char *nat_defined(NatEnv *env, NatObject *receiver, char *name) {
    NatObject *obj;
    if (nat_is_constant_name(name)) {
        obj = nat_const_get_or_null(env, receiver, name);
        if (obj) return "constant";
    } else if (nat_is_global_name(name)) {
        obj = nat_global_get(env, name);
        if (obj != nil) return "global-variable";
    } else if (nat_respond_to(env, receiver, name)) {
        return "method";
    }
    return NULL;
}

NatObject *nat_defined_obj(NatEnv *env, NatObject *receiver, char *name) {
    char *result = nat_defined(env, receiver, name);
    if (result) {
        return nat_string(env, result);
    } else {
        return nil;
    }
}

NatObject *nat_send(NatEnv *env, NatObject *receiver, char *sym, size_t argc, NatObject **args, NatBlock *block) { // FIXME: kwargs
    assert(receiver);
    NatObject *klass;
    if (NAT_TYPE(receiver) == NAT_VALUE_INTEGER) {
        klass = Integer;
    } else {
        klass = receiver->singleton_class;
        if (klass) {
            NatObject *matching_class_or_module;
            NatMethod *method = nat_find_method(klass, sym, &matching_class_or_module);
            if (method) {
#ifdef DEBUG_METHOD_RESOLUTION
                if (strcmp(sym, "inspect") != 0) {
                    if (matching_class_or_module == klass) {
                        fprintf(stderr, "Method %s found on the singleton klass of %s\n", sym, nat_send(env, receiver, "inspect", 0, NULL, NULL)->str);
                    } else {
                        fprintf(stderr, "Method %s found on %s, which is an ancestor of the singleton klass of %s\n", sym, matching_class_or_module->class_name, nat_send(env, receiver, "inspect", 0, NULL, NULL)->str);
                    }
                }
#endif
                return nat_call_method_on_class(env, klass, receiver->klass, sym, receiver, argc, args, NULL, block);
            }
        }
        klass = receiver->klass;
    }
#ifdef DEBUG_METHOD_RESOLUTION
    if (strcmp(sym, "inspect") != 0) {
        fprintf(stderr, "Looking for method %s in the klass hierarchy of %s\n", sym, nat_send(env, receiver, "inspect", 0, NULL, NULL)->str);
    }
#endif
    return nat_call_method_on_class(env, klass, klass, sym, receiver, argc, args, NULL, block);
}

// supply an empty array and it will be populated with the method names as symbols
void nat_methods(NatEnv *env, NatObject *array, NatObject *klass) {
    struct hashmap_iter *iter;
    for (iter = hashmap_iter(&klass->methods); iter; iter = hashmap_iter_next(&klass->methods, iter))
    {
        char *name = (char *)hashmap_iter_get_key(iter);
        nat_array_push(array, nat_symbol(env, name));
    }
    for (size_t i=0; i<klass->included_modules_count; i++) {
        NatObject *module = klass->included_modules[i];
        for (iter = hashmap_iter(&module->methods); iter; iter = hashmap_iter_next(&module->methods, iter))
        {
            char *name = (char *)hashmap_iter_get_key(iter);
            nat_array_push(array, nat_symbol(env, name));
        }
    }
    if (klass->superclass) {
        return nat_methods(env, array, klass->superclass);
    }
}

// returns the method and sets matching_class_or_module to where the method was found
NatMethod *nat_find_method(NatObject *klass, char *method_name, NatObject **matching_class_or_module) {
    assert(NAT_TYPE(klass) == NAT_VALUE_CLASS);

    NatMethod *method = hashmap_get(&klass->methods, method_name);
    if (method) {
        *matching_class_or_module = klass;
        return method;
    }

    for (size_t i=0; i<klass->included_modules_count; i++) {
        NatObject *module = klass->included_modules[i];
        method = hashmap_get(&module->methods, method_name);
        if (method) {
            *matching_class_or_module = module;
            return method;
        }
    }

    if (klass->superclass) {
        return nat_find_method(klass->superclass, method_name, matching_class_or_module);
    } else {
        return NULL;
    }
}

NatObject *nat_call_method_on_class(NatEnv *env, NatObject *klass, NatObject *instance_class, char *method_name, NatObject *self, size_t argc, NatObject **args, struct hashmap *kwargs, NatBlock *block) {
    assert(klass != NULL);
    assert(NAT_TYPE(klass) == NAT_VALUE_CLASS);

    NatObject *matching_class_or_module;
    NatMethod *method = nat_find_method(klass, method_name, &matching_class_or_module);
    if (method) {
#ifdef DEBUG_METHOD_RESOLUTION
        if (strcmp(method_name, "inspect") != 0) {
            fprintf(stderr, "Calling method %s from %s\n", method_name, matching_class_or_module->class_name);
        }
#endif
        NatEnv *closure_env;
        if (method->env) {
            closure_env = method->env;
        } else if (NAT_TYPE(self) == NAT_VALUE_CLASS) {
            // FIXME: not sure if this is proper, but it works
            closure_env = self->env;
        } else {
            closure_env = matching_class_or_module->env;
        }
        NatEnv *e = nat_build_block_env(closure_env, env);
        e->file = env->file;
        e->line = env->line;
        e->method_name = method_name;
        return method->fn(e, self, argc, args, NULL, block);
    } else {
        NAT_RAISE(env, nat_const_get(env, Object, "NoMethodError"), "undefined method `%s' for %s", method_name, nat_send(env, instance_class, "inspect", 0, NULL, NULL)->str);
    }
}

int nat_respond_to(NatEnv *env, NatObject *obj, char *name) {
    NatObject *matching_class_or_module;
    // FIXME: I don't think we need to check both singleton_class and klass since singleton_class inherits from the klass
    if (NAT_TYPE(obj) == NAT_VALUE_INTEGER) {
        NatObject *klass = Integer;
        if (nat_find_method(klass, name, &matching_class_or_module)) {
            return TRUE;
        } else {
            return FALSE;
        }
    } else if (obj->singleton_class && nat_find_method(obj->singleton_class, name, &matching_class_or_module)) {
        return TRUE;
    } else if (nat_find_method(obj->klass, name, &matching_class_or_module)) {
        return TRUE;
    } else {
        return FALSE;
    }
}

NatBlock *nat_block(NatEnv *env, NatObject *self, NatObject* (*fn)(NatEnv*, NatObject*, size_t, NatObject**, struct hashmap*, NatBlock*)) {
    NatBlock *block = GC_MALLOC(sizeof(NatBlock));
    block->env = env;
    block->self = self;
    block->fn = fn;
    return block;
}

NatObject *nat_run_block(NatEnv *env, NatBlock *the_block, size_t argc, NatObject **args, struct hashmap *kwargs, NatBlock *block) {
    NatEnv *e = nat_build_block_env(the_block->env, env);
    return the_block->fn(e, the_block->self, argc, args, kwargs, block);
}

NatObject *nat_proc(NatEnv *env, NatBlock *block) {
    NatObject *obj = nat_new(env, nat_const_get(env, Object, "Proc"), 0, NULL, NULL, NULL);
    obj->type = NAT_VALUE_PROC;
    obj->block = block;
    return obj;
}

NatObject *nat_lambda(NatEnv *env, NatBlock *block) {
    NatObject *lambda = nat_proc(env, block);
    lambda->lambda = TRUE;
    return lambda;
}

// "0x" + up to 16 hex chars + NULL terminator
#define NAT_OBJECT_POINTER_LENGTH 2 + 16 + 1

char *nat_object_pointer_id(NatObject *obj) {
    char *ptr = GC_MALLOC_ATOMIC(NAT_OBJECT_POINTER_LENGTH);
    snprintf(ptr, NAT_OBJECT_POINTER_LENGTH, "%p", obj);
    return ptr;
}

uint64_t nat_next_object_id(NatEnv *env) {
    return (*env->global_env->next_object_id)++;
}

void nat_grow_string(NatObject *obj, size_t capacity) {
    size_t len = strlen(obj->str);
    assert(capacity >= len);
    obj->str = GC_REALLOC(obj->str, capacity + 1);
    obj->str_cap = capacity;
}

void nat_grow_string_at_least(NatObject *obj, size_t min_capacity) {
    size_t capacity = obj->str_cap;
    if (capacity >= min_capacity)
        return;
    if (capacity > 0 && min_capacity <= capacity * NAT_STRING_GROW_FACTOR) {
        nat_grow_string(obj, capacity * NAT_STRING_GROW_FACTOR);
    } else {
        nat_grow_string(obj, min_capacity);
    }
}

void nat_string_append(NatObject *str, char *s) {
    size_t new_len = strlen(s);
    if (new_len == 0) return;
    size_t total_len = str->str_len + new_len;
    nat_grow_string_at_least(str, total_len);
    strcat(str->str, s);
    str->str_len = total_len;
}

void nat_string_append_char(NatObject *str, char c) {
    size_t total_len = str->str_len + 1;
    nat_grow_string_at_least(str, total_len);
    str->str[total_len - 1] = c;
    str->str[total_len] = 0;
    str->str_len = total_len;
}

void nat_string_append_nat_string(NatObject *str, NatObject *str2) {
    if (str2->str_len == 0) return;
    size_t total_len = str->str_len + str2->str_len;
    nat_grow_string_at_least(str, total_len);
    strcat(str->str, str2->str);
    str->str_len = total_len;
}

NatObject* nat_sprintf(NatEnv *env, char *format, ...) {
    va_list args;
    va_start(args, format);
    NatObject *out = nat_vsprintf(env, format, args);
    va_end(args);
    return out;
}

NatObject* nat_vsprintf(NatEnv *env, char *format, va_list args) {
    NatObject *out = nat_string(env, "");
    char c, c2;
    size_t len = strlen(format);
    for (size_t i=0; i<len; i++) {
        c = format[i];
        if (c == '%') {
            c2 = format[++i];
            switch (c2) {
                case 's':
                    nat_string_append(out, va_arg(args, char*));
                    break;
                case 'i':
                case 'd':
                    nat_string_append(out, int_to_string(va_arg(args, int)));
                    break;
                /*
                   case 'S':
                   nat_string_append_nat_string(out, va_arg(args, NatObject*));
                   break;
                case 'z':
                    mal_string_append(out, pr_str(mal_number(va_arg(args, size_t)), 1));
                    break;
                case 'p':
                    mal_string_append(out, pr_str(mal_number((size_t)va_arg(args, NatObject*)), 1));
                    break;
                   */
                case '%':
                    nat_string_append_char(out, '%');
                    break;
                default:
                    fprintf(stderr, "Unknown format specifier: %%%c", c2);
                    abort();
            }
        } else {
            nat_string_append_char(out, c);
        }
    }
    return out;
}

NatObject *nat_dup(NatEnv *env, NatObject *obj) {
    NatObject *copy;
    switch (NAT_TYPE(obj)) {
        case NAT_VALUE_ARRAY:
            copy = nat_array(env);
            for (size_t i=0; i<obj->ary_len; i++) {
                nat_array_push(copy, obj->ary[i]);
            }
            return copy;
        case NAT_VALUE_STRING:
            return nat_string(env, obj->str);
        case NAT_VALUE_SYMBOL:
            return nat_symbol(env, obj->symbol);
        case NAT_VALUE_CLASS:
        case NAT_VALUE_EXCEPTION:
        case NAT_VALUE_FALSE:
        case NAT_VALUE_INTEGER:
        case NAT_VALUE_MODULE:
        case NAT_VALUE_NIL:
        case NAT_VALUE_OTHER:
        case NAT_VALUE_PROC:
        case NAT_VALUE_TRUE:
            memcpy(copy, obj, sizeof(NatObject));
            return copy;
        default:
            fprintf(stderr, "I don't know how to dup this kind of object.\n");
            abort();
    }
}

NatObject *nat_not(NatEnv *env, NatObject *val) {
    if (nat_truthy(val)) {
        return false_obj;
    } else {
        return true_obj;
    }
}

void nat_alias(NatEnv *env, NatObject *self, char *new_name, char *old_name) {
    if (NAT_TYPE(self) == NAT_VALUE_INTEGER || NAT_TYPE(self) == NAT_VALUE_SYMBOL) {
        NAT_RAISE(env, nat_const_get(env, Object, "TypeError"), "no klass to make alias");
    }
    if (nat_is_main_object(self)) {
        self = self->klass;
    }
    NatObject *klass = self;
    if (NAT_TYPE(self) != NAT_VALUE_CLASS && NAT_TYPE(self) != NAT_VALUE_MODULE) {
        klass = nat_singleton_class(env, self);
    }
    NatObject *matching_class_or_module;
    NatMethod *method = nat_find_method(klass, old_name, &matching_class_or_module);
    if (!method) {
        NAT_RAISE(env, nat_const_get(env, Object, "NameError"), "undefined method `%s' for klass `%s'", old_name, nat_send(env, klass, "inspect", 0, NULL, NULL)->str);
    }
    hashmap_remove(&klass->methods, new_name);
    hashmap_put(&klass->methods, new_name, method);
}

void nat_run_at_exit_handlers(NatEnv *env) {
    NatObject *at_exit_handlers = nat_global_get(env, "$NAT_at_exit_handlers");
    assert(at_exit_handlers);
    for (int i=at_exit_handlers->ary_len-1; i>=0; i--) {
        NatObject *proc = at_exit_handlers->ary[i];
        assert(proc);
        assert(NAT_TYPE(proc) == NAT_VALUE_PROC);
        nat_run_block(env, proc->block, 0, NULL, NULL, NULL);
    }
}

void nat_print_exception_with_backtrace(NatEnv *env, NatObject *exception) {
    assert(NAT_TYPE(exception) == NAT_VALUE_EXCEPTION);
    if (exception->backtrace->ary_len > 0) {
        fprintf(stderr, "Traceback (most recent call last):\n");
        for (int i=exception->backtrace->ary_len-1; i>0; i--) {
            NatObject *line = exception->backtrace->ary[i];
            assert(NAT_TYPE(line) == NAT_VALUE_STRING);
            fprintf(stderr, "        %d: from %s\n", i, line->str);
        }
        fprintf(stderr, "%s: ", exception->backtrace->ary[0]->str);
    }
    fprintf(stderr, "%s (%s)\n", exception->message, exception->klass->class_name);
}

void nat_handle_top_level_exception(NatEnv *env, int run_exit_handlers) {
    NatObject *exception = env->exception;
    assert(exception);
    assert(NAT_TYPE(exception) == NAT_VALUE_EXCEPTION);
    env->jump_buf = NULL;
    if (nat_is_a(env, exception, nat_const_get(env, Object, "SystemExit"))) {
        NatObject *status_obj = nat_ivar_get(env, exception, "@status");
        if (run_exit_handlers) nat_run_at_exit_handlers(env);
        if (NAT_TYPE(status_obj) == NAT_VALUE_INTEGER) {
            int64_t val = NAT_INT_VALUE(status_obj);
            if (val >= 0 && val <= 255) {
                exit(val);
            } else {
                exit(1);
            }
        } else {
            exit(1);
        }
    } else {
        nat_print_exception_with_backtrace(env, exception);
    }
}
