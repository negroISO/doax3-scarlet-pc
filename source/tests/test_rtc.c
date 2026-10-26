#undef NDEBUG
#include "../src/runtime_rtc.c"
#include <assert.h>
#include <stdlib.h>
uintptr_t runtime_lookup(const RuntimeExport *e,size_t n,const char *s) { (void)e; (void)n; (void)s; abort(); }
int main(void) {
    DateTime date;
    int64_t seconds=-1;
    assert(rtc_set_time_t(&date,1709164800)==0);
    assert(date.year==2024 && date.month==2 && date.day==29);
    assert(rtc_get_time_t(&date,&seconds)==0 && seconds==1709164800);
    struct { uint64_t tick, guard; } slot={123,UINT64_C(0x1122334455667788)};
    assert(rtc_add_hours(&slot.tick,&slot.tick,2)==0 && slot.tick==7200000123);
    assert(slot.guard==UINT64_C(0x1122334455667788));
    assert(rtc_add_hours(&slot.tick,&slot.tick,-2)==0 && slot.tick==123);
    assert(rtc_add_hours(&slot.tick,&slot.tick,INT64_MIN)!=0);
    uint64_t filetime;
    assert(rtc_set_time_t(&date,0)==0);
    assert(rtc_win32_time(&date,&filetime)==0 && filetime==UINT64_C(116444736000000000));
    puts("RTC test: ok");
}
