/* Authored metadata regression: submitted records outlive mutable incoming data. */
#include "rsx_gpu_submission.h"
#include <stdlib.h>
static unsigned checks,failures;
static void check(int ok,const char *name){++checks;if(!ok){++failures;fprintf(stderr,"FAIL %s\n",name);}}
int main(int argc,char **argv){
 rsx_submission_row row={0},readback;rsx_submission_header h;char path[512];FILE *f;
 int legacy=argc>2 && !strcmp(argv[2],"--reuse-live-records");if(argc<2)return 2;
 check(sizeof(row)==80 && sizeof(h)==40,"version1 layout");
 check(rsx_submission_commit(),"disabled commit");row.fp=0x1111;row.vertices=6;
 check(rsx_submission_begin(argv[1],9,0x1000,0x2000) && rsx_submission_draw(&row),"record first draw");
 row.fp=0x2222;if(legacy)s_gpu_submission.rows[0]=row;
 check(rsx_submission_commit(),"commit before execution");
 check(rsx_submission_begin(argv[1],10,0x1000,0x2000) && rsx_submission_draw(&row) && rsx_submission_commit(),"next batch");
 snprintf(path,sizeof(path),"%s/submission_00000001.bin",argv[1]);f=fopen(path,"rb");if(!f)return 2;
 if(fread(&h,1,sizeof(h),f)!=sizeof(h) || fread(&readback,1,sizeof(readback),f)!=sizeof(readback))return 2;fclose(f);
 check(readback.fp==0x1111 && readback.vertices==6,"immutable submitted record");
 check(h.magic==0x53585352 && h.version==1 && h.serial==1 && h.count==1 && h.frame==9 && h.list==0x1000 && h.queue==0x2000,"batch identity");
 s_gpu_submission.h.serial=0;check(rsx_submission_begin(argv[1],11,0,0) && !rsx_submission_commit(),"refuse overwrite");s_gpu_submission.active=0;s_gpu_submission.h.serial=2;
 check(rsx_submission_begin(argv[1],12,0,0),"new batch after rejected overwrite");
 for(unsigned i=0;i<RSX_SUBMISSION_MAX_DRAWS;++i)if(!rsx_submission_draw(&row))return 2;
 check(!rsx_submission_draw(&row),"record bound");s_gpu_submission.bytes=RSX_SUBMISSION_BUDGET;
 check(!rsx_submission_commit(),"aggregate budget");
 printf("checks=%u failures=%u\n",checks,failures);return failures?1:0;
}
