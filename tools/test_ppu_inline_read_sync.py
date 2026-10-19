"""Real generated inline/raw loads must acquire device-owned guest bytes."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile
from ppu_lifter import HEADER_PREAMBLE

parser=argparse.ArgumentParser()
parser.add_argument('--compiler')
args=parser.parse_args()
root=Path(__file__).resolve().parents[1]
source=r'''
#include "ppu_recomp.h"
#include "ps3emu/guest_read_sync.h"
#include <stdio.h>
#undef vm_read8
#undef vm_read16
#undef vm_read32
#undef vm_read64
static uint8_t memory[0x30000];
uint8_t* vm_base=memory;
uint32_t ppu_vm_size=sizeof(memory), ppu_hle_inject_base=0;
int vm_inline_ok=1;
uint8_t vm_read8(uint64_t a) { (void)a; return 0; }
uint16_t vm_read16(uint64_t a) { (void)a; return 0; }
uint32_t vm_read32(uint64_t a) { (void)a; return 0; }
uint64_t vm_read64(uint64_t a) { (void)a; return 0; }
static unsigned calls, failures;
static void sync_bytes(uint32_t address,uint32_t bytes,void* opaque) {
    (void)opaque; ++calls; memset(memory+address,0x2a,bytes);
}
#define CHECK(expr) do { if(!(expr)) { ++failures; fprintf(stderr,"FAIL %s\n",#expr); } } while(0)
int main(void) {
    vm_read_sync_set_handler(sync_bytes,NULL);
    CHECK(vm_read32_inl(0x20000)==0 && calls==0);
    CHECK(vm_read_sync_mark_range(0x11000,0x1000)==0);
    CHECK(vm_read8_inl(0x11000)==0x2a && calls==1);
    CHECK(vm_read16_inl(0x11002)==0x2a2a && calls==2);
    CHECK(vm_read32_inl(0x11004)==0x2a2a2a2a && calls==3);
    CHECK(vm_read64_inl(0x11008)==UINT64_C(0x2a2a2a2a2a2a2a2a) && calls==4);
    CHECK(vm_read64_inl(0x10fff)==UINT64_C(0x2a2a2a2a2a2a2a2a) && calls==5);
    CHECK(VM_RAW_LD(uint32_t,0x11020)==0x2a2a2a2a && calls==6);
    uint64_t v[2]; vm_raw_ld16(v,0x11030);
    CHECK(v[0]==UINT64_C(0x2a2a2a2a2a2a2a2a) && v[1]==v[0] && calls==8);
    printf("inline/raw load checks=9 failures=%u sync_calls=%u\n",failures,calls);
    return failures?1:0;
}
'''
with tempfile.TemporaryDirectory(prefix='ppu-inline-sync-') as tmp:
    out=Path(tmp);(out/'ppu_recomp.h').write_text(HEADER_PREAMBLE,encoding='utf-8')
    (out/'test.c').write_text(source,encoding='utf-8')
    compiler=args.compiler or os.environ.get('MV_CLANG' if os.name=='nt' else 'CC','clang-cl' if os.name=='nt' else 'cc')
    binary=out/('test.exe' if os.name=='nt' else 'test')
    inputs=[str(out/'test.c'),str(root/'runtime/memory/guest_read_sync.c')]
    if os.name=='nt':
        command=[compiler,'/nologo','/TC','/O1','/UNDEBUG','/I'+str(out),'/I'+str(root/'include'),*inputs,'/Fe'+str(binary),'/Fo'+str(out)+'/']
    else:
        command=[compiler,'-std=gnu17','-O1','-UNDEBUG','-I'+str(out),'-I'+str(root/'include'),*inputs,'-o',str(binary)]
    subprocess.run(command,check=True,timeout=60)
    raise SystemExit(subprocess.run([str(binary)],timeout=10).returncode)
