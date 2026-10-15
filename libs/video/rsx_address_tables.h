/* Included after vp_upload_retire. Immutable tables survive every queued draw.
 * Grow by another legal-size heap, never overwrite a live descriptor. The
 * renderer's checked batch retirement is the sole reclamation boundary. */
#ifndef RSX_ADDRESS_TABLES_H
#define RSX_ADDRESS_TABLES_H
#include "rsx_d3d12_address.h"
typedef struct RsxAddressHeap {
    struct RsxAddressHeap* next;
    ID3D12DescriptorHeap* heap;
    unsigned count;
    D3D12_SAMPLER_DESC tables[128][16];
} RsxAddressHeap;
static RsxAddressHeap* s_address_heaps;
static ID3D12DescriptorHeap* s_address_bound;

static void address_fail(const char* message)
{
    fprintf(stderr, "[RSXADDRESS] %s\n", message); abort();
}
static void address_retire(void)
{
    if (!vp_upload_retire(NULL)) address_fail("GPU retirement failed");
    while (s_address_heaps) {
        RsxAddressHeap* h = s_address_heaps;
        s_address_heaps = h->next;
        h->heap->lpVtbl->Release(h->heap); free(h);
    }
    s_address_bound = NULL;
}
static void address_record(D3D12DrawRecord* dr)
{
    const rsx_state* st = s_d3d.current_rsx_state;
    if (!st) address_fail("draw has no guest state");
    for (unsigned u = 0; u < sizeof(dr->tex) / sizeof(dr->tex[0]); ++u) {
        dr->tex[u].address = st->textures[u].address;
        dr->tex[u].border = st->textures[u].border_color;
    }
}
static int address_record_matches(const D3D12DrawRecord* dr)
{
    const rsx_state* st = s_d3d.current_rsx_state;
    if (!st) return 0;
    for (unsigned u = 0; u < sizeof(dr->tex) / sizeof(dr->tex[0]); ++u)
        if (dr->tex[u].address != st->textures[u].address ||
            dr->tex[u].border != st->textures[u].border_color) return 0;
    return 1;
}
/* Returns true when changing heaps invalidated the caller's cached SRV table. */
static int address_bind(const D3D12DrawRecord* dr, ID3D12PipelineState* pso)
{
    unsigned mask = pso == s_d3d.pipeline_state_vp ? 1u : 0u;
    int found = pso == s_d3d.pipeline_state_vp || pso == s_d3d.pipeline_state_vp_color;
    for (int i = 0; i < s_d3d.vp_fp_n; ++i)
        if (s_d3d.vp_fp[i].pso == pso) {
            mask = s_d3d.vp_fp[i].sampler_mask; found = 1; break;
        }
    if (!found) address_fail("active PSO sampler identity is absent");
    D3D12_SAMPLER_DESC desc[16];
    for (unsigned u = 0; u < 16; ++u) {
        unsigned address = u == 0 ? 0x30303u : 0x10101u, border = 0;
        if ((mask & (1u << u)) && u < sizeof(dr->tex) / sizeof(dr->tex[0]) && dr->tex[u].set) {
            address = dr->tex[u].address; border = dr->tex[u].border;
        }
        if (!rsx_address_sampler(u, address, border, &desc[u])) {
            fprintf(stderr, "[RSXADDRESS] unsupported address u%u value=%08X fp=%08X\n",
                    u, address, dr->fp_addr); abort();
        }
    }
    RsxAddressHeap* h = s_address_heaps;
    unsigned slot = 0;
    for (; h; h = h->next) {
        for (slot = 0; slot < h->count; ++slot)
            if (!memcmp(desc, h->tables[slot], sizeof(desc))) break;
        if (slot < h->count) break;
    }
    if (!h) {
        h = s_address_heaps;
        if (!h || h->count == 128) {
            h = (RsxAddressHeap*)calloc(1, sizeof(*h));
            if (!h) address_fail("table allocation failed");
            D3D12_DESCRIPTOR_HEAP_DESC hd = {0};
            hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
            hd.NumDescriptors = 2048; hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
            HRESULT hr = s_d3d.device->lpVtbl->CreateDescriptorHeap(s_d3d.device, &hd,
                &IID_ID3D12DescriptorHeap, (void**)&h->heap);
            if (FAILED(hr) || !h->heap) address_fail("sampler heap allocation failed");
            h->next = s_address_heaps; s_address_heaps = h;
        }
        slot = h->count++;
        memcpy(h->tables[slot], desc, sizeof(desc));
        UINT inc = s_d3d.device->lpVtbl->GetDescriptorHandleIncrementSize(
            s_d3d.device, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
        D3D12_CPU_DESCRIPTOR_HANDLE cpu;
        h->heap->lpVtbl->GetCPUDescriptorHandleForHeapStart(h->heap, &cpu);
        cpu.ptr += (SIZE_T)slot * 16 * inc;
        for (unsigned u = 0; u < 16; ++u) {
            s_d3d.device->lpVtbl->CreateSampler(s_d3d.device, &desc[u], cpu);
            cpu.ptr += inc;
        }
    }
    int changed = s_address_bound != h->heap;
    if (changed) {
        ID3D12DescriptorHeap* heaps[] = {s_d3d.srv_heap, h->heap};
        s_d3d.cmd_list->lpVtbl->SetDescriptorHeaps(s_d3d.cmd_list, 2, heaps);
        s_address_bound = h->heap;
    }
    UINT inc = s_d3d.device->lpVtbl->GetDescriptorHandleIncrementSize(
        s_d3d.device, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
    D3D12_GPU_DESCRIPTOR_HANDLE gpu;
    h->heap->lpVtbl->GetGPUDescriptorHandleForHeapStart(h->heap, &gpu);
    gpu.ptr += (UINT64)slot * 16 * inc;
    s_d3d.cmd_list->lpVtbl->SetGraphicsRootDescriptorTable(s_d3d.cmd_list, 3, gpu);
    return changed;
}
#endif
