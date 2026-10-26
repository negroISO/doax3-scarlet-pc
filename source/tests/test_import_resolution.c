#undef NDEBUG
#include "runtime.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    /* Scoped-only map entry, canonical runtime input, bare and dump-local input. */
    assert(!strcmp(runtime_symbol("7uhBFWRAS60#libkernel"),"sceKernelClearEventFlag"));
    assert(!strcmp(runtime_symbol("7uhBFWRAS60"),"sceKernelClearEventFlag"));
    assert(!strcmp(runtime_symbol("7uhBFWRAS60#3#4"),"sceKernelClearEventFlag"));
    assert(!strcmp(runtime_symbol("az-0R6eviZ0#libSceUserService"),"sceUserServiceInitialize2"));
    assert(!strcmp(runtime_symbol("yH17Q6NWtVg#libSceUserService"),"sceUserServiceGetEvent"));
    assert(runtime_symbol("7uhBFWRAS6")==NULL);
    assert(runtime_symbol("not-a-NID#libkernel")==NULL);
    assert(runtime_symbol(NULL)==NULL);
    puts("import resolution test: ok");
}
