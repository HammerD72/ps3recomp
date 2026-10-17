/* Opt-in input observation after the HLE call. No report or guest state changes. */
#ifndef PS3RECOMP_PPU_PAD_TRACE_H
#define PS3RECOMP_PPU_PAD_TRACE_H
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <cstdint>
namespace ps3_pad_trace_detail {
struct Trace {
    FILE* file = nullptr;
    std::mutex mutex;
    uint64_t rows = 0, bytes = 0, empty = 0;
    uint64_t callers[128] = {};
    unsigned caller_count = 0;
    Trace() {
        const char* path = std::getenv("PAD_PACKET_TRACE_FILE");
        if (path && *path) {
            file = std::fopen(path, "wbx");
            if (!file) fail("cannot exclusively create output");
        }
    }
    ~Trace() { if (file && std::fclose(file)) std::fprintf(stderr,"[PADTRACE] close failed\n"); }
    [[noreturn]] static void fail(const char* reason) {
        std::fprintf(stderr,"[PADTRACE] %s\n", reason); std::abort();
    }
};
inline unsigned be16(const uint8_t* p) { return (unsigned(p[0])<<8)|p[1]; }
}
static void ppu_pad_trace(const char* name, uint64_t thread, uint32_t lr,
                          uint32_t port, uint32_t address, uint64_t result,
                          const uint8_t* memory, uint64_t r2, uint64_t r30) {
    using namespace ps3_pad_trace_detail;
    if (!name || std::strcmp(name,"cellPadGetData")) return;
    static Trace trace;
    if (!trace.file) return;
    std::lock_guard<std::mutex> lock(trace.mutex);
    uint32_t length=0,buttons=0; unsigned words[64]={};
    if (!result) {
        if (!memory || !address || uint64_t(address)+132>UINT64_C(0x100000000)) Trace::fail("invalid returned packet range");
        const uint8_t* data=memory+address;
        length=(be16(data)<<16)|be16(data+2);
        for (unsigned i=0;i<64;i++) words[i]=be16(data+4+2*i);
        buttons=words[2]|(words[3]<<8);
    }
    uint64_t caller=(uint64_t(port)<<32)|lr;
    bool first=true;
    for(unsigned i=0;i<trace.caller_count;i++) if(trace.callers[i]==caller)first=false;
    if(first) {
        if(trace.caller_count==128) Trace::fail("caller quota exceeded");
        trace.callers[trace.caller_count++]=caller;
    }
    if(!result && !length) { trace.empty++; if(!first)return; }
    if(trace.rows==100000)Trace::fail("record quota exceeded");
    char row[1600];
    auto now=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    int n=std::snprintf(row,sizeof row,"{\"seq\":%llu,\"utc_ms\":%lld,\"thread\":%llu,\"lr\":\"%08X\",\"port\":%u,\"address\":\"%08X\",\"result\":\"%llX\",\"len\":%u,\"buttons\":\"%04X\",\"empty_reads\":%llu,\"r2\":\"%llX\",\"r30\":\"%llX\",\"words\":[",(unsigned long long)trace.rows,(long long)now,(unsigned long long)thread,lr,port,address,(unsigned long long)result,length,buttons,(unsigned long long)trace.empty,(unsigned long long)r2,(unsigned long long)r30);
    for(unsigned i=0;i<64;i++) {
        if(n<0 || size_t(n)>=sizeof row)Trace::fail("record formatting failed");
        n+=std::snprintf(row+n,sizeof row-size_t(n),"%s%u",i?",":"",words[i]);
    }
    if(n<0 || size_t(n)+3>=sizeof row)Trace::fail("record formatting overflow");
    row[n++]=']';row[n++]='}';row[n++]='\n';
    if(trace.bytes+unsigned(n)>64u*1024u*1024u)Trace::fail("byte quota exceeded");
    if(std::fwrite(row,1,size_t(n),trace.file)!=size_t(n)||std::fflush(trace.file))Trace::fail("record write failed");
    trace.rows++;trace.bytes+=unsigned(n);
}
#endif
