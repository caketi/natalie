#include "natalie.h"
#include "builtin.h"

NatObject *TrueClass_to_s(NatEnv *env, NatObject *self, size_t argc, NatObject **args, struct hashmap *kwargs, NatBlock *block) {
    assert(NAT_TYPE(self) == NAT_VALUE_TRUE);
    NAT_ASSERT_ARGC(0);
    return nat_string(env, "true");
}
