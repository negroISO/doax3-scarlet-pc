/* Reference-port maps contain both bare NIDs and dump-local library scopes. */
#include "runtime.h"
#include <string.h>

static const struct { const char *nid, *symbol; } import_names[]={
#include "import_names.inc"
};

const char *runtime_symbol(const char *nid) {
    if (!nid) return NULL;
    /* Prefer the exact spelling, then compare the NID part of BOTH sides.
     * Stripping only the query loses scoped-only entries such as ClearEventFlag. */
    for (size_t i=0;i<sizeof(import_names)/sizeof(*import_names);++i)
        if (!strcmp(nid,import_names[i].nid)) return import_names[i].symbol;
    size_t n=strcspn(nid,"#");
    for (size_t i=0;i<sizeof(import_names)/sizeof(*import_names);++i) {
        const char *entry=import_names[i].nid;
        if (strcspn(entry,"#")==n && !strncmp(nid,entry,n)) return import_names[i].symbol;
    }
    return NULL;
}
