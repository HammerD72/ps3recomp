/* Actual production table/record helper with authored COM storage adapters. */
#define WIN32_LEAN_AND_MEAN
#define CINTERFACE
#define COBJMACROS
#include <windows.h>
#include <d3d12.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <stdint.h>
typedef struct { struct { unsigned address, border_color; } textures[16]; } rsx_state;
typedef struct { struct { unsigned address, border; int set; } tex[16]; unsigned fp_addr; } D3D12DrawRecord;
typedef struct FakeHeap {
    ID3D12DescriptorHeap api;
    D3D12_SAMPLER_DESC descriptors[2048];
} FakeHeap;
static unsigned heaps_live, releases, retired, root_binds, heap_binds;
static int retire_ok = 1;
static ULONG STDMETHODCALLTYPE release_heap(ID3D12DescriptorHeap* api)
{ assert(retired); heaps_live--; releases++; free(api); return 0; }
static D3D12_CPU_DESCRIPTOR_HANDLE* STDMETHODCALLTYPE cpu_start(ID3D12DescriptorHeap* api, D3D12_CPU_DESCRIPTOR_HANDLE* out)
{ out->ptr = (SIZE_T)((FakeHeap*)api)->descriptors; return out; }
static D3D12_GPU_DESCRIPTOR_HANDLE* STDMETHODCALLTYPE gpu_start(ID3D12DescriptorHeap* api, D3D12_GPU_DESCRIPTOR_HANDLE* out)
{ out->ptr = (UINT64)(uintptr_t)((FakeHeap*)api)->descriptors; return out; }
static ID3D12DescriptorHeapVtbl heap_vt = {
    .Release = release_heap, .GetCPUDescriptorHandleForHeapStart = cpu_start,
    .GetGPUDescriptorHandleForHeapStart = gpu_start
};
static HRESULT STDMETHODCALLTYPE create_heap(ID3D12Device* device, const D3D12_DESCRIPTOR_HEAP_DESC* desc, REFIID iid, void** out)
{
    (void)device; (void)iid;
    assert(desc->NumDescriptors == 2048 && desc->Type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
    FakeHeap* h = (FakeHeap*)calloc(1, sizeof(*h)); assert(h);
    h->api.lpVtbl = &heap_vt; *out = h; heaps_live++; return S_OK;
}
static UINT STDMETHODCALLTYPE increment(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type)
{ (void)device; assert(type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER); return sizeof(D3D12_SAMPLER_DESC); }
static void STDMETHODCALLTYPE create_sampler(ID3D12Device* device, const D3D12_SAMPLER_DESC* desc, D3D12_CPU_DESCRIPTOR_HANDLE cpu)
{ (void)device; memcpy((void*)cpu.ptr, desc, sizeof(*desc)); }
static ID3D12DeviceVtbl device_vt = {
    .CreateDescriptorHeap = create_heap, .GetDescriptorHandleIncrementSize = increment,
    .CreateSampler = create_sampler
};
static void STDMETHODCALLTYPE bind_heaps(ID3D12GraphicsCommandList* list, UINT count, ID3D12DescriptorHeap* const* heaps)
{ (void)list; assert(count == 2 && heaps[1]); heap_binds++; }
static void STDMETHODCALLTYPE bind_root(ID3D12GraphicsCommandList* list, UINT index, D3D12_GPU_DESCRIPTOR_HANDLE gpu)
{ (void)list; assert(index == 3 && gpu.ptr); root_binds++; }
static ID3D12GraphicsCommandListVtbl list_vt = {
    .SetDescriptorHeaps = bind_heaps, .SetGraphicsRootDescriptorTable = bind_root
};
static ID3D12Device device = {&device_vt};
static ID3D12GraphicsCommandList list = {&list_vt};
static struct {
    const rsx_state* current_rsx_state;
    ID3D12Device* device;
    ID3D12GraphicsCommandList* cmd_list;
    ID3D12DescriptorHeap* srv_heap;
    ID3D12PipelineState *pipeline_state_vp, *pipeline_state_vp_color;
    int vp_fp_n;
    struct { ID3D12PipelineState* pso; unsigned sampler_mask; } vp_fp[1];
} s_d3d;
static int vp_upload_retire(void* context) { (void)context; if (retire_ok) retired++; return retire_ok; }
#include "rsx_address_tables.h"
int main(int argc, char** argv)
{
    s_d3d.device = &device; s_d3d.cmd_list = &list;
    ID3D12PipelineState* pso = (ID3D12PipelineState*)(uintptr_t)0x44;
    s_d3d.vp_fp_n = 1; s_d3d.vp_fp[0].pso = pso; s_d3d.vp_fp[0].sampler_mask = 2;
    rsx_state guest = {0}; s_d3d.current_rsx_state = &guest;
    D3D12DrawRecord dr = {0}; dr.tex[1].set = 1;
    guest.textures[1].address = 0x40404; address_record(&dr);
    assert(address_record_matches(&dr));
    guest.textures[1].address = 0x30303; assert(!address_record_matches(&dr));
    guest.textures[1].address = 0x40404;
    assert(address_bind(&dr, pso));
    RsxAddressHeap* first = s_address_heaps;
    D3D12_SAMPLER_DESC saved = ((FakeHeap*)first->heap)->descriptors[1];
    for (unsigned i = 1; i <= 130; ++i) {
        guest.textures[1].border_color = i; address_record(&dr);
        address_bind(&dr, pso);
    }
    assert(heaps_live == 2 && first->count == 128 && s_address_heaps->count == 3);
    assert(!memcmp(&saved, &((FakeHeap*)first->heap)->descriptors[1], sizeof(saved)));
    guest.textures[1].border_color = 0; address_record(&dr);
    assert(address_bind(&dr, pso)); /* Return to old heap invalidates SRV cache. */
    assert(!address_bind(&dr, pso)); /* Same heap is stable. */
    assert(heaps_live == 2 && releases == 0 && root_binds == 133 && heap_binds == 3);
    assert(rsx_address_sampler_mask("rsx_samp[7]") == 128);
    if (argc > 1 && !strcmp(argv[1], "--fail-retire")) retire_ok = 0;
    address_retire();
    assert(heaps_live == 0 && releases == 2 && retired == 1 && !s_address_heaps && !s_address_bound);
    puts("PASS: record identity, immutable 131 tables, heap rollover/rebind and checked retirement");
    return 0;
}
