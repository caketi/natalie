#include "natalie.h"
#include "builtin.h"

NatObject *BasicObject_not(NatEnv *env, NatObject *self, size_t argc, NatObject **args, struct hashmap *kwargs, NatBlock *block) {
    return nat_not(env, self);
}

NatObject *BasicObject_eqeq(NatEnv *env, NatObject *self, size_t argc, NatObject **args, struct hashmap *kwargs, NatBlock *block) {
    NAT_ASSERT_ARGC(1);
    NatObject *arg = args[0];
    if (self == arg) {
        return NAT_TRUE;
    } else {
        return NAT_FALSE;
    }
}

NatObject *BasicObject_neq(NatEnv *env, NatObject *self, size_t argc, NatObject **args, struct hashmap *kwargs, NatBlock *block) {
    NAT_ASSERT_ARGC(1);
    NatObject *arg = args[0];
    return BasicObject_not(env, nat_send(env, self, "==", 1, &arg, NULL), 0, NULL, NULL, NULL);
}

NatObject *BasicObject_instance_eval(NatEnv *env, NatObject *self, size_t argc, NatObject **args, struct hashmap *kwargs, NatBlock *block) {
    if (argc > 0 || !block) {
        NAT_RAISE(env, nat_const_get(env, NAT_OBJECT, "ArgumentError"), "Natalie only supports instance_eval with a block");
    }
    NatEnv e;
    nat_build_block_env(&e, &block->env, env);
    NatObject *self_for_eval = self;
    // I *think* this is right... instance_eval, when called on a class/module,
    // evals with self set to the singleton class
    if (NAT_TYPE(self) == NAT_VALUE_CLASS || NAT_TYPE(self) == NAT_VALUE_MODULE) {
        self_for_eval = nat_singleton_class(env, self);
    }
    return block->fn(&e, self_for_eval, 0, NULL, NULL, NULL);
}
