/* Exercise the production event-flag implementation, including DOAX3's 0x22 wait. */
#undef NDEBUG
#include "../src/runtime_kernel.c"
#include <assert.h>
/* Unrelated process/clock services are never called in this focused test. */
uintptr_t runtime_lookup(const RuntimeExport *e,size_t n,const char *s) { (void)e; (void)n; (void)s; abort(); }
int *runtime_errno(void) { abort(); }
void runtime_report(void) { abort(); }

int main(void) {
    uint64_t id=0, result=0;
    assert(create_event_flag(&id,"test",0,1,NULL)==0);
    /* OR matches a single requested bit; AND requires both. No implicit clear. */
    assert(poll_event_flag(id,3,1,&result)!=0);
    assert(poll_event_flag(id,3,2,&result)==0 && result==1);
    assert(poll_event_flag(id,1,2,&result)==0);
    assert(clear_event_flag(id,~UINT64_C(1))==0);
    assert(poll_event_flag(id,1,2,&result)!=0);
    assert(set_event_flag(id,5)==0);
    assert(wait_event_flag(id,3,0x22,&result,NULL)==0 && result==5);
    assert(poll_event_flag(id,4,2,&result)==0 && result==4);
    assert(poll_event_flag(id,1,2,&result)!=0);
    assert(set_event_flag(id,1)==0);
    assert(poll_event_flag(id,1,0x12,&result)==0 && result==5);
    assert(poll_event_flag(id,4,2,&result)!=0);
    assert(poll_event_flag(id,0,2,&result)!=0);
    assert(poll_event_flag(id,1,0x32,&result)!=0);
    assert(delete_event_flag(id)==0);
    puts("event flags test: ok");
}
