/* Private synthetic syscall contract test; contains no guest payload. */
#include "sys_memory.h"
#include <stdio.h>
#include <stdlib.h>
uint8_t* vm_base;
uint32_t ppu_vm_size;
uint32_t ppu_hle_inject_base;
static uint32_t read_output(uint32_t ea) {
    const unsigned char* p=vm_to_host(ea);
    return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];
}
int main(void) {
    if(vm_init()!=CELL_OK){fprintf(stderr,"VM initialization failed; rejected trial\n");return 2;}
    lv2_syscall_table table={0};sys_memory_init(&table);
    uint32_t out=0x10000;
    ppu_context ctx={0};
    for(unsigned i=0;i<8;i++) {
        ctx.gpr[3]=out+16;
        if(sys_memory_get_user_memory_size(&ctx)!=CELL_OK)return 2;
        uint32_t total=read_output(out+16),available=read_output(out+20);
        ctx.gpr[3]=24*1024*1024;ctx.gpr[4]=SYS_MEMORY_PAGE_SIZE_1M;ctx.gpr[5]=out;
        int64_t rc=sys_memory_allocate(&ctx);
        if(rc!=CELL_OK){
            fprintf(stderr,"RED: allocation %u failed status=%08X total=%u available=%u request=%u bump=%08X\n",i+1,(uint32_t)rc,total,available,24*1024*1024,g_sys_mem_bump_ptr);
            vm_shutdown();return available>=24*1024*1024?1:2;
        }
        uint32_t addr=read_output(out);
        fprintf(stdout,"allocated %u addr=%08X size=01800000 available_before=%u\n",i+1,addr,available);
    }
    vm_shutdown();puts("Capacity contract passed: eight 24MiB allocations within advertised 213MiB");return 0;
}
