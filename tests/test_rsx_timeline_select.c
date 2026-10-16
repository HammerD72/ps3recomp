#include "rsx_timeline_select.h"
#include <stdio.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); return 1; } } while(0)
int main(void) {
    rsx_timeline_selection s;
    uint32_t programs[] = {0,0xABC1,0x1111,0xABC1,0x2221}, out[8]={99};
    CHECK(rsx_timeline_parse("100 0 2 5", 8, &s));
    CHECK(!s.by_fp && s.target==0x100 && s.count==3);
    CHECK(rsx_timeline_resolve(&s,programs,5,out) && out[0]==0 && out[2]==5);
    CHECK(rsx_timeline_parse("100 fp:ABC1 fp:2221",8,&s));
    CHECK(rsx_timeline_resolve(&s,programs,5,out) && out[0]==1 && out[1]==4);
    programs[1]=0;
    CHECK(rsx_timeline_resolve(&s,programs,5,out) && out[0]==3 && out[1]==4);
    programs[4]=0; out[0]=99;
    CHECK(!rsx_timeline_resolve(&s,programs,5,out) && out[0]==99);
    CHECK(rsx_timeline_parse("100 fp:2221 fp:ABC1",8,&s));
    programs[0]=0xABC1; programs[4]=0x2221;
    CHECK(!rsx_timeline_resolve(&s,programs,5,out));
    const char* bad[]={"","100","0 1","-1 1","100 1 1","100 2 1","100 8","100 -1","100 fp:0","100 fp:","100 fp:FFFFFFFFF","100 fp:1 2","100 1 fp:2","100 0 1 2 3 4 5 6 7 8","100 fp:1x"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);++i) CHECK(!rsx_timeline_parse(bad[i],8,&s));
    puts("PASS timeline selection: indices, shifted FP identities, missing/order, invalid input");
    return 0;
}
