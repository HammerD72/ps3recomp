#include <stdio.h>
#define RSX_SHADER_CACHE_ENTRIES 2
#define RSX_SHADER_CACHE_BYTES 1024
#include "rsx_shader_cache.h"
typedef struct {int refs;} Blob;
static void retain(void* c,void* b){(void)c;((Blob*)b)->refs++;}
static void release(void* c,void* b){(void)c;((Blob*)b)->refs--;}
static unsigned checks,failures;
static void check(int ok,const char* label){checks++;if(!ok){failures++;printf("FAIL %s\n",label);}}
int main(void){
 rsx_shader_cache c={0};rsx_shader_blob_ops ops={NULL,retain,release};Blob a={1},b={1},d={1};char source[]="one";
 rsx_shader_key k={source,3,"name","main","ps_5_0",0,0},v=k;
 check(rsx_shader_cache_put(&c,&k,&a,20,&ops)&&a.refs==2,"cache owns separate reference");
 void* got=rsx_shader_cache_get(&c,&k,&ops);check(got==&a&&a.refs==3,"caller owns hit");release(NULL,got);
 source[0]='X';v.source="one";got=rsx_shader_cache_get(&c,&v,&ops);check(got==&a,"key copied before source mutation");release(NULL,got);
 c.entries[0].hash=rsx_shader_key_hash(&k);check(!rsx_shader_cache_get(&c,&k,&ops),"hash collision compares complete source");c.entries[0].hash=rsx_shader_key_hash(&v);
 k=v;k.flags1=1;check(!rsx_shader_cache_get(&c,&k,&ops),"flags1 distinct");k=v;k.flags2=1;check(!rsx_shader_cache_get(&c,&k,&ops),"flags2 distinct");
 k=v;k.name="other";check(!rsx_shader_cache_get(&c,&k,&ops),"source name distinct");k=v;k.entry="other";check(!rsx_shader_cache_get(&c,&k,&ops),"entry distinct");k=v;k.target="vs_5_0";check(!rsx_shader_cache_get(&c,&k,&ops),"target distinct");
 k=v;k.source="two";check(rsx_shader_cache_put(&c,&k,&b,20,&ops),"second source");got=rsx_shader_cache_get(&c,&v,&ops);release(NULL,got);
 k.source="tri";check(rsx_shader_cache_put(&c,&k,&d,20,&ops)&&b.refs==1&&a.refs==2&&d.refs==2,"LRU preserves recently used source");
 check(!rsx_shader_cache_put(&c,&k,&b,1024,&ops)&&b.refs==1&&c.count==2,"budget refusal preserves ownership");
 check(rsx_shader_cache_put(&c,&v,&b,20,&ops)&&a.refs==1&&b.refs==2&&c.count==2,"replacement releases old owner");
 rsx_shader_cache_clear(&c,&ops);check(a.refs==1&&b.refs==1&&d.refs==1&&!c.count&&!c.bytes,"clear releases every cache owner");
 printf("checks=%u failures=%u\n",checks,failures);return failures?1:0;
}
