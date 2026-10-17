/* Opt-in immutable CPU metadata for actual GPU submissions. No guest payloads. */
#ifndef RSX_GPU_SUBMISSION_H
#define RSX_GPU_SUBMISSION_H
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#define RSX_SUBMISSION_MAX_DRAWS 2048
#define RSX_SUBMISSION_BUDGET (64ull*1024*1024)
typedef struct rsx_submission_row {
 uint32_t op,vertices,vb_offset,is_vp,fp,control,vs,shader,rt,zeta,cmask,blend,width,height;
 uint64_t depth_stencil;
 uint32_t viewport_w,viewport_h,texture0,texture1;
} rsx_submission_row;
typedef struct rsx_submission_header {
 uint32_t magic,version,serial,count;
 uint64_t frame,list,queue;
} rsx_submission_header;
static struct {
 rsx_submission_header h;rsx_submission_row rows[RSX_SUBMISSION_MAX_DRAWS];
 uint64_t bytes;char directory[448];int active;
} s_gpu_submission;
static int rsx_submission_begin(const char *directory,uint64_t frame,uintptr_t list,uintptr_t queue) {
 if(s_gpu_submission.active)return 0;
 if(!directory || !*directory)return 1;
 if(strlen(directory)>=sizeof(s_gpu_submission.directory))return 0;
 strcpy(s_gpu_submission.directory,directory);s_gpu_submission.h.magic=0x53585352;
 s_gpu_submission.h.version=1;++s_gpu_submission.h.serial;s_gpu_submission.h.count=0;
 s_gpu_submission.h.frame=frame;s_gpu_submission.h.list=list;s_gpu_submission.h.queue=queue;
 s_gpu_submission.active=1;return 1;
}
static int rsx_submission_draw(const rsx_submission_row *row) {
 if(!s_gpu_submission.active)return 1;
 if(s_gpu_submission.h.count>=RSX_SUBMISSION_MAX_DRAWS)return 0;
 s_gpu_submission.rows[s_gpu_submission.h.count++]=*row;return 1;
}
static int rsx_submission_commit(void) {
 char path[512];FILE *f;size_t bytes;int n,ok;
 if(!s_gpu_submission.active)return 1;
 bytes=sizeof(s_gpu_submission.h)+s_gpu_submission.h.count*sizeof(rsx_submission_row);
 if(bytes+s_gpu_submission.bytes>RSX_SUBMISSION_BUDGET)return 0;
 n=snprintf(path,sizeof(path),"%s/submission_%08u.bin",s_gpu_submission.directory,s_gpu_submission.h.serial);
 if(n<0 || (size_t)n>=sizeof(path))return 0;
 f=fopen(path,"wbx");if(!f)return 0;
 ok=fwrite(&s_gpu_submission.h,1,sizeof(s_gpu_submission.h),f)==sizeof(s_gpu_submission.h);
 if(ok)ok=fwrite(s_gpu_submission.rows,sizeof(rsx_submission_row),s_gpu_submission.h.count,f)==s_gpu_submission.h.count;
 if(fclose(f))ok=0;
 if(!ok)return 0;
 s_gpu_submission.bytes+=bytes;s_gpu_submission.active=0;return 1;
}
#endif
