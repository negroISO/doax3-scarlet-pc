#include "runtime.h"
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
typedef int32_t (ABI *Module)(uint16_t);
typedef int32_t (ABI *Init)(const void *,void *);
typedef int32_t (ABI *Param)(uint32_t,int32_t *);
typedef int32_t (ABI *List)(uint32_t,void *,uint32_t,uint32_t *);
typedef int32_t (ABI *Info)(uint32_t,const void *,void *);
typedef int32_t (ABI *Mount)(uint32_t,const void *,void *);
typedef int32_t (ABI *Unmount)(const void *);
#define GET(t,n) ((t)runtime_content_resolve(n))
/* The file system as runtime_content.c sees it: /app0 is a folder with two add-ons. */
static char app0_dir[512];
static char mounted_guest[64], mounted_host[512];
int runtime_file_host_path(const char *guest,char *out,size_t size) {
    if (strcmp(guest,"/app0") || !app0_dir[0]) return 2;
    snprintf(out,size,"%s",app0_dir); return 0;
}
int runtime_file_mount(const char *guest,const char *host) {
    snprintf(mounted_guest,sizeof(mounted_guest),"%s",guest);
    snprintf(mounted_host,sizeof(mounted_host),"%s",host); return 0;
}
void runtime_file_unmount(const char *guest) { if (!strcmp(guest,mounted_guest)) mounted_guest[0]=0; }
static void write_sfo(const char *dir,const char *category,const char *content_id) {
    char path[600]; snprintf(path,sizeof(path),"%s/sce_sys",dir);
    mkdir(dir,0755); mkdir(path,0755);
    snprintf(path,sizeof(path),"%s/sce_sys/param.sfo",dir);
    /* Two utf-8 entries: CATEGORY and CONTENT_ID (the PS4 param.sfo layout). */
    unsigned char sfo[256]={0};
    const char *keys[]={"CATEGORY","CONTENT_ID"}; const char *values[]={category,content_id};
    const uint32_t key_table=20+2*16, data_table=key_table+32;
    memcpy(sfo,"\0PSF",4); sfo[4]=1;
    memcpy(sfo+8,&key_table,4); memcpy(sfo+12,&data_table,4);
    uint32_t count=2; memcpy(sfo+16,&count,4);
    uint16_t key_offset=0; uint32_t data_offset=0;
    for (unsigned i=0;i<2;++i) {
        unsigned char *e=sfo+20+16*i; const uint16_t format=0x0204;
        const uint32_t length=(uint32_t)strlen(values[i])+1, max_length=48;
        memcpy(e,&key_offset,2); memcpy(e+2,&format,2); memcpy(e+4,&length,4);
        memcpy(e+8,&max_length,4); memcpy(e+12,&data_offset,4);
        memcpy(sfo+key_table+key_offset,keys[i],strlen(keys[i])+1);
        memcpy(sfo+data_table+data_offset,values[i],length);
        key_offset=(uint16_t)(key_offset+strlen(keys[i])+1); data_offset+=max_length;
    }
    FILE *f=fopen(path,"wb"); assert(f); fwrite(sfo,1,data_table+96,f); fclose(f);
}
int addons_main(void);
int many_addons_main(void);
int main(int argc,char **argv) {
    if (argc>1 && !strcmp(argv[1],"--addons")) return addons_main();
    if (argc>1 && !strcmp(argv[1],"--many-addons")) return many_addons_main();
    Module load=GET(Module,"g8cM39EUZ6o#libSceSysmodule");
    Module loaded=GET(Module,"fMP5NHUOaMk#libSceSysmodule");
    Module unload=GET(Module,"eR2bZFAAU0Q#libSceSysmodule");
    Init init=GET(Init,"R9lA82OraNs#libSceAppContent");
    Param param=GET(Param,"99b82IKXpH4#libSceAppContent");
    List list=GET(List,"xnd8BJzAxmk#libSceAppContent");
    assert(load && loaded && unload && init && param && list);
    assert(!runtime_content_resolve("g8cM39EUZ6o#libkernel"));
    if (argc>1 && !strcmp(argv[1],"--missing")) { load(0xb4); return 99; }
    const uint32_t profile[]={1,13,0x80000000,0,7};
    runtime_content_configure(profile);
    if (argc>1 && !strcmp(argv[1],"--unknown")) {
        /* Other system modules are host-provided: reference counted, independent. */
        assert((uint32_t)loaded(0xb5)==0x805a1001);
        assert(load(0xb5)==0 && loaded(0xb5)==0 && (uint32_t)loaded(0xb4)==0x805a1001);
        assert(unload(0xb5)==0 && (uint32_t)loaded(0xb5)==0x805a1001);
        puts("PASS: independent module references"); return 0;
    }
    assert((uint32_t)loaded(0)==0x805a1000);
    assert((uint32_t)loaded(0xb4)==0x805a1001);
    assert((uint32_t)unload(0xb4)==0x805a1001);
    assert(load(0xb4)==0 && load(0xb4)==0 && loaded(0xb4)==0);
    unsigned char initial[32]={0};
    struct { unsigned char boot[40]; uint32_t canary; } b;
    memset(&b,0xa5,sizeof(b));
    assert((uint32_t)init(NULL,b.boot)==0x80d90002);
    initial[31]=1; assert((uint32_t)init(initial,b.boot)==0x80d90002); initial[31]=0;
    assert(b.boot[0]==0xa5 && init(initial,b.boot)==0);
    for (unsigned i=0;i<40;++i) assert(b.boot[i]==0);
    assert(b.canary==0xa5a5a5a5);
    b.boot[0]=0xcc; assert((uint32_t)init(initial,b.boot)==0x80d90003 && b.boot[0]==0xcc);
    struct { int32_t value; uint32_t canary; } result={-1,0x1234};
    for (unsigned i=0;i<5;++i) {
        assert(param(i,&result.value)==0 && (uint32_t)result.value==profile[i]);
        assert(result.canary==0x1234);
    }
    result.value=77;
    assert((uint32_t)param(5,&result.value)==0x80d90002 && result.value==77);
    assert((uint32_t)param(0,NULL)==0x80d90002);
    unsigned char entries[48]; memset(entries,0xaa,sizeof(entries));
    struct { uint32_t hits,canary; } h={99,0xabcdef};
    assert(list(0,NULL,0,&h.hits)==0 && h.hits==0 && h.canary==0xabcdef);
    assert(list(0,entries,2,&h.hits)==0 && h.hits==0);
    for (unsigned i=0;i<sizeof(entries);++i) assert(entries[i]==0xaa);
    assert((uint32_t)list(0,NULL,0,NULL)==0x80d90002);
    assert(unload(0xb4)==0 && loaded(0xb4)==0);
    assert((uint32_t)init(initial,b.boot)==0x80d90003);
    assert(unload(0xb4)==0 && (uint32_t)loaded(0xb4)==0x805a1001);
    assert(load(0xb4)==0 && init(initial,b.boot)==0 && unload(0xb4)==0);
    puts("PASS: AppContent lifecycle, profile and output boundaries");
    return 0;
}

/* Add-ons (issue #16): folders of category "ac" in the game folder, labels from CONTENT_ID. */
int addons_main(void) {
    Module load=GET(Module,"g8cM39EUZ6o#libSceSysmodule");
    Init init=GET(Init,"R9lA82OraNs#libSceAppContent");
    List list=GET(List,"xnd8BJzAxmk#libSceAppContent");
    Info info=GET(Info,"m47juOmH0VE#libSceAppContent");
    Mount mount=GET(Mount,"VANhIWcqYak#libSceAppContent");
    Unmount unmount=GET(Unmount,"3rHWaV-1KC4#libSceAppContent");
    assert(info && mount && unmount);
    const char *tmp=getenv("TEMP"); if (!tmp) tmp=getenv("TMPDIR"); if (!tmp) tmp="/tmp";
    snprintf(app0_dir,sizeof(app0_dir),"%s/content-test-%d",tmp,(int)getpid());
    char dir[600];
    mkdir(app0_dir,0755);
    snprintf(dir,sizeof(dir),"%s/TO1886GAMEDLC100",app0_dir);
    write_sfo(dir,"ac","UP9000-CUSA00035_00-TO1886GAMEDLC100");
    snprintf(dir,sizeof(dir),"%s/sce_sys_like_game",app0_dir);
    write_sfo(dir,"gd","UP9000-CUSA00035_00-THEORDER1886GAME");
    const uint32_t profile[]={3,0,0,0,0};
    runtime_content_configure(profile);
    unsigned char initial[32]={0}, boot[40];
    assert(load(0xb4)==0 && init(initial,boot)==0);
    uint32_t hits=0;
    assert(list(0,NULL,0,&hits)==0 && hits==1);
    struct { char label[17]; char padding[3]; uint32_t status; } entries[2];
    memset(entries,0xaa,sizeof(entries));
    assert(list(0,entries,2,&hits)==0 && hits==1);
    assert(!strcmp(entries[0].label,"TO1886GAMEDLC100") && entries[0].status==4);
    assert((unsigned char)entries[1].label[0]==0xaa); /* unused slots untouched */
    struct { char label[17]; char padding[3]; } label={"TO1886GAMEDLC100",{0}}, other={"TO1886GAMEDLC300",{0}};
    struct { char label[17]; char padding[3]; uint32_t status; } one;
    assert(info(0,&label,&one)==0 && one.status==4 && !strcmp(one.label,"TO1886GAMEDLC100"));
    assert((uint32_t)info(0,&other,&one)==0x80d90007);
    char point[16]; memset(point,0x55,sizeof(point));
    assert(mount(0,&label,point)==0 && !strcmp(point,"/addcont0"));
    assert(!strcmp(mounted_guest,"/addcont0") && strstr(mounted_host,"TO1886GAMEDLC100"));
    assert((uint32_t)mount(0,&other,point)==0x80d90005);
    assert(unmount(point)==0 && !mounted_guest[0]);
    puts("PASS: AppContent add-ons listed and mounted");
    return 0;
}

/* Fortune ships more than sixteen independent packs; no filesystem order is assumed. */
int many_addons_main(void) {
    enum { COUNT=73 };
    Module load=GET(Module,"g8cM39EUZ6o#libSceSysmodule");
    Init init=GET(Init,"R9lA82OraNs#libSceAppContent");
    List list=GET(List,"xnd8BJzAxmk#libSceAppContent");
    Mount mount=GET(Mount,"VANhIWcqYak#libSceAppContent");
    const char *tmp=getenv("TEMP"); if (!tmp) tmp=getenv("TMPDIR"); if (!tmp) tmp="/tmp";
    snprintf(app0_dir,sizeof(app0_dir),"%s/content-many-%d",tmp,(int)getpid());
    assert(!mkdir(app0_dir,0755));
    for (unsigned i=0;i<COUNT;++i) {
        char label[17], content[48], dir[600];
        snprintf(label,sizeof(label),"FODLC%011u",i);
        snprintf(content,sizeof(content),"HP0106-CUSA04555_00-%s",label);
        snprintf(dir,sizeof(dir),"%s/pack-%u",app0_dir,i);
        write_sfo(dir,"ac",content);
    }
    char invalid[600]; snprintf(invalid,sizeof(invalid),"%s/invalid",app0_dir);
    write_sfo(invalid,"ac","HP0106-CUSA04555_00-FODLC000000000001"); /* seventeen-byte label */
    const uint32_t profile[]={3,0,0,0,0}; runtime_content_configure(profile);
    unsigned char initial[32]={0}, boot[40];
    assert(load(0xb4)==0 && init(initial,boot)==0);
    uint32_t hits=0; assert(!list(0,NULL,0,&hits) && hits==COUNT);
    struct Entry { char label[17]; char padding[3]; uint32_t status; } entries[COUNT+1];
    memset(entries,0xa5,sizeof(entries));
    assert(!list(0,entries,COUNT,&hits) && hits==COUNT);
    assert((unsigned char)entries[COUNT].label[0]==0xa5);
    assert(!list(0,entries,8,&hits) && hits==8); /* count describes only populated entries */
    unsigned seen[COUNT]={0};
    for (unsigned i=0;i<COUNT;++i) {
        struct { char label[17]; char padding[3]; } label={0};
        snprintf(label.label,sizeof(label.label),"FODLC%011u",i);
        char point[16]={0};
        assert(!mount(0,&label,point) && !strncmp(point,"/addcont",8));
        const unsigned slot=(unsigned)strtoul(point+8,NULL,10);
        assert(slot<COUNT && !seen[slot]++);
    }
    for (unsigned i=0;i<=COUNT;++i) {
        char path[700], dir[600];
        if (i==COUNT) snprintf(dir,sizeof(dir),"%s/invalid",app0_dir);
        else snprintf(dir,sizeof(dir),"%s/pack-%u",app0_dir,i);
        snprintf(path,sizeof(path),"%s/sce_sys/param.sfo",dir); assert(!unlink(path));
        snprintf(path,sizeof(path),"%s/sce_sys",dir); assert(!rmdir(path));
        assert(!rmdir(dir));
    }
    assert(!rmdir(app0_dir));
    puts("PASS: all 73 add-ons enumerated and mounted; caller bounds and malformed labels checked");
    return 0;
}
