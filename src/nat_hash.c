#include "natalie.h"
#include "nat_hash.h"

NatObject *Hash_inspect(NatEnv *env, NatObject *self, size_t argc, NatObject **args, struct hashmap *kwargs, NatBlock *block) {
    NAT_ASSERT_ARGC(0);
    assert(NAT_TYPE(self) == NAT_VALUE_HASH);
    NatObject *out = nat_string(env, "{");
    NatHashIter *iter;
    size_t last_index = self->hashmap.num_entries - 1;
    size_t index;
    for (iter = nat_hash_iter(env, self), index = 0; iter; iter = nat_hash_iter_next(env, self, iter), index++) {
        NatObject *key_repr = nat_send(env, iter->key, "inspect", 0, NULL, NULL);
        nat_string_append(out, key_repr->str);
        nat_string_append(out, "=>");
        NatObject *val_repr = nat_send(env, iter->val, "inspect", 0, NULL, NULL);
        nat_string_append(out, val_repr->str);
        if (index < last_index) {
            nat_string_append(out, ", ");
        }
    }
    nat_string_append_char(out, '}');
    return out;
}

NatObject *Hash_ref(NatEnv *env, NatObject *self, size_t argc, NatObject **args, struct hashmap *kwargs, NatBlock *block) {
    NAT_ASSERT_ARGC(1);
    assert(NAT_TYPE(self) == NAT_VALUE_HASH);
    NatObject *key = args[0];
    NatObject *val = nat_hash_get(env, self, key);
    if (val) {
        return val;
    } else {
        return nil;
    }
}

NatObject *Hash_refeq(NatEnv *env, NatObject *self, size_t argc, NatObject **args, struct hashmap *kwargs, NatBlock *block) {
    NAT_ASSERT_ARGC(2);
    assert(NAT_TYPE(self) == NAT_VALUE_HASH);
    NatObject *key = args[0];
    NatObject *val = args[1];
    nat_hash_put(env, self, key, val);
    return val;
}

NatObject *Hash_delete(NatEnv *env, NatObject *self, size_t argc, NatObject **args, struct hashmap *kwargs, NatBlock *block) {
    NAT_ASSERT_ARGC(1);
    assert(NAT_TYPE(self) == NAT_VALUE_HASH);
    NatObject *key = args[0];
    NatObject *val = nat_hash_delete(env, self, key);
    if (val) {
        return val;
    } else {
        return nil;
    }
}

NatObject *Hash_size(NatEnv *env, NatObject *self, size_t argc, NatObject **args, struct hashmap *kwargs, NatBlock *block) {
    NAT_ASSERT_ARGC(0);
    assert(NAT_TYPE(self) == NAT_VALUE_HASH);
    return nat_integer(env, self->hashmap.num_entries);
}

NatObject *Hash_eqeq(NatEnv *env, NatObject *self, size_t argc, NatObject **args, struct hashmap *kwargs, NatBlock *block) {
    NAT_ASSERT_ARGC(1);
    assert(NAT_TYPE(self) == NAT_VALUE_HASH);
    NatObject *other = args[0];
    if (NAT_TYPE(other) != NAT_VALUE_HASH) {
        return false_obj;
    }
    if (self->hashmap.num_entries != other->hashmap.num_entries) {
        return false_obj;
    }
    NatHashIter *iter;
    NatObject *other_val;
    for (iter = nat_hash_iter(env, self); iter; iter = nat_hash_iter_next(env, self, iter)) {
        other_val = nat_hash_get(env, other, iter->key);
        if (!nat_truthy(nat_send(env, iter->val, "==", 1, &other_val, NULL))) {
            return false_obj;
        }
    }
    return true_obj;
}

NatObject *Hash_each(NatEnv *env, NatObject *self, size_t argc, NatObject **args, struct hashmap *kwargs, NatBlock *block) {
    NAT_ASSERT_ARGC(0);
    assert(NAT_TYPE(self) == NAT_VALUE_HASH);
    assert(block);
    NatHashIter *iter;
    NatObject **block_args = GC_MALLOC(2 * sizeof(NatObject));
    for (iter = nat_hash_iter(env, self); iter; iter = nat_hash_iter_next(env, self, iter)) {
        block_args[0] = iter->key;
        block_args[1] = iter->val;
        nat_run_block(env, block, 2, block_args, NULL, NULL);
    }
    return self;
}

NatObject *Hash_keys(NatEnv *env, NatObject *self, size_t argc, NatObject **args, struct hashmap *kwargs, NatBlock *block) {
    NAT_ASSERT_ARGC(0);
    assert(NAT_TYPE(self) == NAT_VALUE_HASH);
    NatObject *array = nat_array(env);
    NatHashIter *iter;
    NatObject **block_args = GC_MALLOC(2 * sizeof(NatObject));
    for (iter = nat_hash_iter(env, self); iter; iter = nat_hash_iter_next(env, self, iter)) {
        nat_array_push(array, iter->key);
    }
    return array;
}

NatObject *Hash_values(NatEnv *env, NatObject *self, size_t argc, NatObject **args, struct hashmap *kwargs, NatBlock *block) {
    NAT_ASSERT_ARGC(0);
    assert(NAT_TYPE(self) == NAT_VALUE_HASH);
    NatObject *array = nat_array(env);
    NatHashIter *iter;
    NatObject **block_args = GC_MALLOC(2 * sizeof(NatObject));
    for (iter = nat_hash_iter(env, self); iter; iter = nat_hash_iter_next(env, self, iter)) {
        nat_array_push(array, iter->val);
    }
    return array;
}
