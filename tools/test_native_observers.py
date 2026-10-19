"""Execute bounded native profiling observers without private title inputs."""
import json
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = r'''
#define _GNU_SOURCE
#include <stdint.h>
#include <string.h>
#include "ps3emu/loadtime_trace.h"
#include "thread_name.h"
#ifdef _WIN32
#include <windows.h>
static DWORD WINAPI worker(void* unused) {
#else
#include <pthread.h>
static void* worker(void* unused) {
#endif
    (void)unused;
    for (int i=0;i<40;i++)
        loadtime_event_path("fixture", "dur_us=1", "asset with spaces/quote\"=test.rpk");
    return 0;
}
int main(int argc, char** argv) {
    (void)argv;
    if (argc>1) { loadtime_event("disabled", ""); return loadtime_on()!=0; }
#ifdef _WIN32
    ps3_thread_setname((uintptr_t)GetCurrentThread(), "observer-test");
    typedef HRESULT (WINAPI *getdesc_t)(HANDLE,PWSTR*);
    getdesc_t getdesc=(getdesc_t)(void(*)(void))GetProcAddress(GetModuleHandleA("kernel32.dll"),"GetThreadDescription");
    if (getdesc) {
        PWSTR name=NULL;
        if (FAILED(getdesc(GetCurrentThread(),&name))) return 2;
        int wrong=wcscmp(name,L"observer-test"); LocalFree(name); if(wrong)return 3;
    }
    HANDLE threads[8];
    for(int i=0;i<8;i++) { threads[i]=CreateThread(NULL,0,worker,NULL,0,NULL); if(!threads[i])return 4; }
    WaitForMultipleObjects(8,threads,TRUE,INFINITE);
    for(int i=0;i<8;i++)CloseHandle(threads[i]);
#else
    ps3_thread_setname(0,"observer-test");
    char name[64];
    if(pthread_getname_np(pthread_self(),name,sizeof name) || strcmp(name,"observer-test"))return 3;
    pthread_t threads[8];
    for(int i=0;i<8;i++)if(pthread_create(&threads[i],NULL,worker,NULL))return 4;
    for(int i=0;i<8;i++)pthread_join(threads[i],NULL);
#endif
    return 0;
}
'''

with tempfile.TemporaryDirectory(prefix='native-observers-') as tmp:
    out=Path(tmp);source=out/'fixture.c';source.write_text(SOURCE)
    exe=out/('fixture.exe' if os.name=='nt' else 'fixture')
    inputs=[str(source),str(ROOT/'runtime/diag/loadtime_trace.c'),str(ROOT/'runtime/platform/thread_name.c')]
    includes=[str(ROOT/'include'),str(ROOT/'runtime/platform')]
    compiler=os.environ.get('MV_CLANG' if os.name=='nt' else 'CC','clang-cl' if os.name=='nt' else 'cc')
    if os.name=='nt':
        cmd=[compiler,'/nologo','/std:c17','/O1','/UNDEBUG','/DLT_MAX_EVENTS=32',
             *['/I'+p for p in includes],*inputs,'/Fe'+str(exe),'/Fo'+str(out)+'/']
    else:
        cmd=[compiler,'-std=c17','-O1','-UNDEBUG','-DLT_MAX_EVENTS=32','-pthread',
             *['-I'+p for p in includes],*inputs,'-o',str(exe)]
    subprocess.run(cmd,check=True,timeout=60)
    env=dict(os.environ,LOADTIME_TRACE='0',LOADTIME_TRACE_FILE=str(out/'events.jsonl'),PS3_THRLOG='1')
    disabled=subprocess.run([exe,'disabled'],env=env,capture_output=True,timeout=15)
    assert disabled.returncode==0 and not disabled.stderr and not (out/'events.jsonl').exists()
    env['LOADTIME_TRACE']='1'
    result=subprocess.run([exe],env=env,capture_output=True,timeout=15)
    assert result.returncode==0, result.stderr
    events=[json.loads(line) for line in (out/'events.jsonl').read_text().splitlines()]
    assert len(events)==34 and events[0]['kind']=='trace_start' and events[-1]['kind']=='trace_truncated'
    assert all(e['path']=='asset with spaces/quote"=test.rpk' for e in events[1:-1])
    assert [e['t_us'] for e in events]==sorted(e['t_us'] for e in events)
    assert b'event budget reached' in result.stderr
    if os.name=='nt': assert b'name=observer-test' in result.stderr
    print('PASS native thread name, disabled tracing, concurrent bounded events, escaping and timestamps')
