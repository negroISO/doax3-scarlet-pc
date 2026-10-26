/* Offline AppContent provider for the configured base-game profile.
 * Add-on content (DLC) is the dumped add-on folders found at start (find_addons), mounted at
 * /addcontN; no downloads, entitlement or license emulation. */
#include "runtime.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define INVALID_ID ((int32_t)0x805a1000)
#define NOT_LOADED ((int32_t)0x805a1001)
#define PARAMETER ((int32_t)0x80d90002)
#define BUSY ((int32_t)0x80d90003)
#define NOT_FOUND ((int32_t)0x80d90005)
#define NO_ENTITLEMENT ((int32_t)0x80d90007)
#define INSTALLED 4u
#define LABEL_OFFSET 20 /* CONTENT_ID "UP9000-CUSA00035_00-TO1886GAMEDLC100": the label follows */
typedef struct { char data[17]; char padding[3]; } Label;
typedef struct { Label label; uint32_t status; } AddcontInfo;
typedef struct { char data[16]; } MountPoint;
_Static_assert(sizeof(AddcontInfo)==24,"SceAppContentAddcontInfo layout");
typedef struct { char label[17]; char title[64]; char host[512]; } Addon;
#define MAX_ADDONS 256
static Addon addons[MAX_ADDONS];
static unsigned addon_count;
static unsigned refs, initialized, configured;
static uint32_t parameters[5];
static unsigned queries, lists;
void runtime_content_configure(const uint32_t values[5]) {
    if (refs || initialized || (values[0]!=1 && values[0]!=3)) {
        fputs("ERROR: invalid or late content profile\n",stderr); exit(1);
    }
    memcpy(parameters,values,sizeof(parameters)); configured=1;
}
static void require_provider(void) {
    if (!configured) { fputs("STOP: AppContent needs --content-profile\n",stderr); exit(21); }
}
/* System modules: every library this eboot imports is provided by the host
 * runtime, so loading one only tracks a reference count. AppContent (0xb4)
 * additionally requires the configured content profile. */
static unsigned module_refs[0x10000];
static ABI int32_t module_load(uint16_t id) {
    if (!id) return INVALID_ID;
    if (id==0xb4) require_provider();
    if (module_refs[id]==UINT32_MAX) { fputs("STOP: module reference overflow\n",stderr); exit(21); }
    ++module_refs[id];
    if (id==0xb4) { refs=module_refs[id]; printf("Runtime: AppContent host provider loaded; references=%u\n",refs); }
    else printf("Runtime: system module 0x%x loaded (host implementation)\n",id);
    return 0;
}
static ABI int32_t module_loaded(uint16_t id) {
    if (!id) return INVALID_ID;
    return module_refs[id] ? 0 : NOT_LOADED;
}
/* Native Fiber probes the optional internal libScePerf module (shadPS4's
 * sysmodule_table.h: 0x80000019). No performance-library module is loaded here. */
static ABI int32_t internal_module_loaded(uint32_t id) {
    return id==UINT32_C(0x80000019) ? NOT_LOADED : INVALID_ID;
}
static ABI int32_t module_unload(uint16_t id) {
    int32_t result=module_loaded(id);
    if (result) return result;
    --module_refs[id];
    if (id==0xb4) { refs=module_refs[id]; if (!refs) initialized=0; }
    return 0;
}
static void require_loaded(void) {
    require_provider();
    if (!refs) { fputs("STOP: AppContent called before module load\n",stderr); exit(21); }
}
/* A string value of a param.sfo, or NULL. */
static const char *sfo_string(const unsigned char *sfo,size_t size,const char *key) {
    if (size<20 || memcmp(sfo,"\0PSF",4)) return NULL;
    uint32_t keys,values,count;
    memcpy(&keys,sfo+8,4); memcpy(&values,sfo+12,4); memcpy(&count,sfo+16,4);
    for (uint32_t i=0;i<count && 20+16*(size_t)i+16<=size;++i) {
        const unsigned char *e=sfo+20+16*i;
        uint16_t key_offset,format; uint32_t length,data_offset;
        memcpy(&key_offset,e,2); memcpy(&format,e+2,2); memcpy(&length,e+4,4); memcpy(&data_offset,e+12,4);
        if (keys+key_offset>=size || values+(size_t)data_offset+length>size || format!=0x0204) continue;
        const char *name=(const char *)sfo+keys+key_offset;
        if (memchr(name,0,size-keys-key_offset) && !strcmp(name,key)) {
            const char *value=(const char *)sfo+values+data_offset;
            return memchr(value,0,length) ? value : NULL;
        }
    }
    return NULL;
}
/* Add-on folders under dir: a sce_sys/param.sfo of category "ac" (The Order's two costume packs
 * sit in the game folder as TO1886GAMEDLC100 and TO1886GAMEDLC200 in the dumps, issue #16). */
static void find_addons(const char *dir) {
    DIR *d=opendir(dir);
    if (!d) return;
    struct dirent *e;
    while ((e=readdir(d))) {
        if (e->d_name[0]=='.') continue;
        char path[1024];
        snprintf(path,sizeof(path),"%s/%s/sce_sys/param.sfo",dir,e->d_name);
        FILE *f=fopen(path,"rb");
        if (!f) continue;
        unsigned char sfo[16384];
        const size_t size=fread(sfo,1,sizeof(sfo),f);
        fclose(f);
        const char *category=sfo_string(sfo,size,"CATEGORY");
        const char *content_id=sfo_string(sfo,size,"CONTENT_ID");
        const char *title=sfo_string(sfo,size,"TITLE");
        if (!category || strcmp(category,"ac") || !content_id) continue;
        /* Standard PS4 content IDs have a 20-byte prefix and a 16-byte label.
         * Never truncate a malformed ID into another installed add-on's label. */
        if (strlen(content_id)!=LABEL_OFFSET+16 || content_id[LABEL_OFFSET-1]!='-') {
            fprintf(stderr,"Runtime: ignoring add-on with malformed CONTENT_ID in %s\n",path);
            continue;
        }
        int duplicate=0;
        for (unsigned i=0;i<addon_count;++i)
            duplicate|=!strcmp(addons[i].label,content_id+LABEL_OFFSET);
        if (duplicate) continue;
        if (addon_count==MAX_ADDONS) {
            fprintf(stderr,"STOP: more than %u installed add-ons; refusing a partial content list\n",MAX_ADDONS);
            closedir(d); exit(21);
        }
        Addon *a=&addons[addon_count];
        memset(a,0,sizeof(*a));
        snprintf(a->label,sizeof(a->label),"%s",content_id+LABEL_OFFSET);
        snprintf(a->title,sizeof(a->title),"%s",title ? title : "");
        snprintf(a->host,sizeof(a->host),"%s/%s",dir,e->d_name);
        ++addon_count;
    }
    closedir(d);
}
static int find_addon(const Label *label) {
    for (unsigned i=0;i<addon_count;++i)
        if (!strncmp(label->data,addons[i].label,sizeof(label->data)-1)) return (int)i;
    return -1;
}
static ABI int32_t content_init(const unsigned char *init,unsigned char *boot) {
    require_loaded();
    if (!init || !boot) return PARAMETER;
    if (initialized) return BUSY;
    /* Only the reserved-zero init profile is implemented. */
    for (unsigned i=0;i<32;++i) if (init[i]) return PARAMETER;
    memset(boot,0,40); initialized=1;
    char app0[512];
    if (!runtime_file_host_path("/app0",app0,sizeof(app0))) find_addons(app0);
    const char *extra=getenv("THEORDER_ADDON_DIR");
    if (extra && *extra) find_addons(extra);
    printf("Runtime: AppContent initialized; offline base-game profile, %u add-on(s)",addon_count);
    for (unsigned i=0;i<addon_count;++i) printf("%s %s (%s)",i ? "," : ":",addons[i].label,addons[i].title);
    putchar('\n');
    return 0;
}
static void require_initialized(void) {
    require_loaded();
    if (!initialized) { fputs("STOP: AppContent query before initialization\n",stderr); exit(21); }
}
static ABI int32_t param_int(uint32_t id,int32_t *out) {
    require_initialized();
    if (id>4 || !out) return PARAMETER;
    memcpy(out,&parameters[id],4); ++queries;
    printf("Runtime: AppContent parameter %u = %d\n",id,*out);
    return 0;
}
static ABI int32_t addon_list(uint32_t service,AddcontInfo *list,uint32_t capacity,uint32_t *hits) {
    require_initialized();
    (void)service; /* any service label (CUSA00785 asks a nonzero one): the same add-ons */
    ++lists;
    if (!capacity || !list) {
        if (!hits) return PARAMETER;
        *hits=addon_count;
        return 0;
    }
    const uint32_t n=addon_count<capacity ? addon_count : capacity;
    for (uint32_t i=0;i<n;++i) {
        memset(&list[i],0,sizeof(list[i]));
        memcpy(list[i].label.data,addons[i].label,strlen(addons[i].label));
        list[i].status=INSTALLED;
    }
    if (hits) *hits=n;
    return 0;
}
static ABI int32_t addon_info(uint32_t service,const Label *label,AddcontInfo *info) {
    require_initialized();
    (void)service;
    if (!label || !info) return PARAMETER;
    const int i=find_addon(label);
    if (i<0) return NO_ENTITLEMENT;
    memset(info,0,sizeof(*info));
    memcpy(info->label.data,addons[i].label,strlen(addons[i].label));
    info->status=INSTALLED;
    return 0;
}
static ABI int32_t addcont_mount(uint32_t service,const Label *label,MountPoint *mount) {
    require_initialized();
    (void)service;
    if (!label || !mount) return PARAMETER;
    const int i=find_addon(label);
    if (i<0) return NOT_FOUND;
    char guest[sizeof(mount->data)];
    const int length=snprintf(guest,sizeof(guest),"/addcont%d",i);
    if (length<0 || (size_t)length>=sizeof(guest)) return PARAMETER;
    if (runtime_file_mount(guest,addons[i].host)) return NOT_FOUND;
    memset(mount->data,0,sizeof(mount->data));
    memcpy(mount->data,guest,strlen(guest));
    printf("Runtime: AppContent add-on %s mounted at %s\n",addons[i].label,guest);
    return 0;
}
static ABI int32_t addcont_unmount(const MountPoint *mount) {
    if (mount && mount->data[0]) {
        char guest[sizeof(mount->data)+1]={0};
        memcpy(guest,mount->data,sizeof(mount->data));
        runtime_file_unmount(guest);
    }
    return 0;
}
static ABI int32_t entitlement_key(uint32_t service,const Label *label,unsigned char *key) {
    (void)service;
    if (!label || !key) return PARAMETER;
    if (find_addon(label)<0) return NOT_FOUND;
    memset(key,0,16);
    return 0;
}
uintptr_t runtime_content_resolve(const char *name) {
    if (!strcmp(name,"ynFKQ5bfGks#libSceSysmodule")) return (uintptr_t)internal_module_loaded;
    if (!strcmp(name,"g8cM39EUZ6o#libSceSysmodule") || !strcmp(name,"sceSysmoduleLoadModule")) return (uintptr_t)module_load;
    if (!strcmp(name,"fMP5NHUOaMk#libSceSysmodule") || !strcmp(name,"sceSysmoduleIsModuleLoaded")) return (uintptr_t)module_loaded;
    if (!strcmp(name,"eR2bZFAAU0Q#libSceSysmodule") || !strcmp(name,"sceSysmoduleUnloadModule")) return (uintptr_t)module_unload;
    if (!strcmp(name,"R9lA82OraNs#libSceAppContent") || !strcmp(name,"sceAppContentInitialize")) return (uintptr_t)content_init;
    if (!strcmp(name,"99b82IKXpH4#libSceAppContent") || !strcmp(name,"sceAppContentAppParamGetInt")) return (uintptr_t)param_int;
    if (!strcmp(name,"xnd8BJzAxmk#libSceAppContent") || !strcmp(name,"sceAppContentGetAddcontInfoList")) return (uintptr_t)addon_list;
    if (!strcmp(name,"m47juOmH0VE#libSceAppContent")) return (uintptr_t)addon_info;
    if (!strcmp(name,"VANhIWcqYak#libSceAppContent") || !strcmp(name,"sceAppContentAddcontMount")) return (uintptr_t)addcont_mount;
    if (!strcmp(name,"3rHWaV-1KC4#libSceAppContent") || !strcmp(name,"sceAppContentAddcontUnmount")) return (uintptr_t)addcont_unmount;
    if (!strcmp(name,"XTWR0UXvcgs#libSceAppContent") || !strcmp(name,"sceAppContentGetEntitlementKey")) return (uintptr_t)entitlement_key;
    return 0;
}
void runtime_content_report(void) {
    printf("Runtime: AppContent references=%u, initialized=%u, parameter queries=%u, list queries=%u\n",refs,initialized,queries,lists);
}
