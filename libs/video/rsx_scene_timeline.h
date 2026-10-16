#include "rsx_timeline_select.h"
/* Opt-in, one-frame GPU target snapshots. No draw filtering or presentation.
 * The arm file contains: hex-target-offset decimal-op-index [...], at most 8.
 * Copies stay in the original command list and restore each target's state.
 * Included after cap_write_bmp and the backend's resource helpers. */
#ifndef RSX_SCENE_TIMELINE_H
#define RSX_SCENE_TIMELINE_H

static int off_rt_find(u32 off);
static void off_rt_transition(int slot, D3D12_RESOURCE_STATES to);

static struct {
    int active, done;
    u32 target, ops[8], count, queued;
    u64 bytes, frame;
    struct {
        ID3D12Resource* resource;
        u32 op, width, height, pitch, format;
    } copies[8];
} s_scene_timeline;

static void scene_timeline_fail(const char* reason)
{
    fprintf(stderr, "[RSXTIMELINE] %s\n", reason);
    abort();
}

/* Keep draw-time sampler registers separately from rendering state. */
static rsx_texture_state s_scene_bound_tex[16];
static rsx_texture_state s_scene_record_tex[MAX_DRAWS][16];
static void scene_timeline_bind(u32 unit, const rsx_texture_state* tex)
{
    if (unit < 16 && getenv("RSX_TIMELINE_ARM_FILE")) s_scene_bound_tex[unit] = *tex;
}
static void scene_timeline_record(u32 slot)
{
    if (slot < MAX_DRAWS && getenv("RSX_TIMELINE_ARM_FILE"))
        memcpy(s_scene_record_tex[slot], s_scene_bound_tex, sizeof s_scene_bound_tex);
}
static void scene_timeline_metadata(u32 op)
{
    D3D12DrawRecord* dr = &s_d3d.draws[op];
    char path[512];
    int n = snprintf(path, sizeof(path), "%s/timeline_frame_%llu_before_op_%u_state.txt",
        getenv("RSX_CAP_DIR"), (unsigned long long)s_scene_timeline.frame, op);
    if (n < 0 || (size_t)n >= sizeof(path)) scene_timeline_fail("metadata path too long");
    FILE* f = fopen(path, "wb");
    if (!f) scene_timeline_fail("metadata open failed");
    fprintf(f, "op=%u fp=%08X exp32=%d vs=%d vertices=%u clear=%d\n",
        op, dr->fp_addr, dr->fp_exp32, dr->vs_idx, dr->vertex_count, dr->is_clear);
    fprintf(f, "surface_format=%08X target=%08X mrt=%08X/%08X/%08X viewport=%u,%u,%u,%u cb_slot=%u\n",
        dr->rt_fmt, dr->rt_off, dr->rt_mrt[0], dr->rt_mrt[1], dr->rt_mrt[2],
        dr->vp_x, dr->vp_y, dr->vp_w, dr->vp_h, dr->cb_slot);
#ifdef RSX_TIMELINE_DRAW_DATA
    RSX_TIMELINE_DRAW_DATA(op, f, path);
#else
    fprintf(f, "draw_data=unavailable\n");
#endif
    for (u32 u = 0; u < sizeof(dr->tex)/sizeof(dr->tex[0]); u++) if (dr->tex[u].set) {
        const rsx_texture_state* t = &s_scene_record_tex[op][u];
        fprintf(f, "t%u resolved=%08X address=%08X filter=%08X control0=%08X border=%08X format=%08X remap=%08X extent=%08X\n",
            u, dr->tex[u].off, t->address, t->filter, t->control0,
            t->border_color, t->format, t->control1, t->image_rect);
    }
    if (ferror(f)) scene_timeline_fail("metadata write failed");
    if (fclose(f)) scene_timeline_fail("metadata close failed");
}

static void scene_timeline_begin(void)
{
    const char* arm = getenv("RSX_TIMELINE_ARM_FILE");
    if (!arm || s_scene_timeline.done || s_scene_timeline.active) return;
    DWORD attr = GetFileAttributesA(arm);
    if (attr == INVALID_FILE_ATTRIBUTES) {
        if (GetLastError() != ERROR_FILE_NOT_FOUND &&
            GetLastError() != ERROR_PATH_NOT_FOUND)
            scene_timeline_fail("arm file query failed");
        return;
    }
    FILE* f = fopen(arm, "r");
    if (!f) scene_timeline_fail("arm file open failed");
    char text[256];
    size_t n = fread(text, 1, sizeof(text) - 1, f);
    if (ferror(f) || !feof(f)) scene_timeline_fail("arm file exceeds limit or read failed");
    fclose(f); text[n] = 0;
    rsx_timeline_selection selection;
    if (!rsx_timeline_parse(text, MAX_DRAWS, &selection) || !getenv("RSX_CAP_DIR"))
        scene_timeline_fail("valid operation/FP selectors and RSX_CAP_DIR required");
    u32 programs[MAX_DRAWS];
    u32 count = s_d3d.draw_count < MAX_DRAWS ? s_d3d.draw_count : MAX_DRAWS;
    if (selection.by_fp)
        for (u32 i = 0; i < count; ++i) programs[i] = s_d3d.draws[i].is_clear ? 0 : s_d3d.draws[i].fp_addr;
    if (!rsx_timeline_resolve(&selection, programs, count, s_scene_timeline.ops)) {
        static int warned;
        if (!warned++) fprintf(stderr, "[RSXTIMELINE] pending: ordered FP identities absent in current submission\n");
        return;
    }
    s_scene_timeline.target = selection.target;
    s_scene_timeline.count = selection.count;
    s_scene_timeline.active = 1;
    s_scene_timeline.frame = s_d3d.frame_count;
    fprintf(stderr, "[RSXTIMELINE] armed target=0x%X frame=%llu count=%u\n",
        s_scene_timeline.target, (unsigned long long)s_scene_timeline.frame,
        s_scene_timeline.count);
}

static void scene_timeline_before_op(u32 op)
{
    u32 i = s_scene_timeline.queued;
    if (!s_scene_timeline.active || i == s_scene_timeline.count ||
        op != s_scene_timeline.ops[i]) return;
    scene_timeline_metadata(op);
    int rt = off_rt_find(s_scene_timeline.target);
    if (rt < 0) scene_timeline_fail("requested offscreen target is absent");
    OffRT* r = &s_d3d.off_rt[rt];
    if (!r->res || !r->w || !r->h || r->w > 4096 || r->h > 4096)
        scene_timeline_fail("invalid target resource or extent");
    u32 bpp = r->dxgi == DXGI_FORMAT_R16G16B16A16_FLOAT ? 8 :
              r->dxgi == DXGI_FORMAT_R32G32B32A32_FLOAT ? 16 : 4;
    u32 pitch = (r->w * bpp + 255) & ~255u;
    u64 bytes = (u64)pitch * r->h;
    if (s_scene_timeline.bytes + bytes > 128ull * 1024 * 1024)
        scene_timeline_fail("128 MiB readback budget exceeded");
    D3D12_HEAP_PROPERTIES hp = {0}; hp.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC rd = {0};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; rd.Width = bytes;
    rd.Height = 1; rd.DepthOrArraySize = 1; rd.MipLevels = 1;
    rd.SampleDesc.Count = 1; rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ID3D12Resource* rb = NULL;
    HRESULT hr = s_d3d.device->lpVtbl->CreateCommittedResource(s_d3d.device,
        &hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COPY_DEST,
        NULL, &IID_ID3D12Resource, (void**)&rb);
    if (FAILED(hr) || !rb) scene_timeline_fail("readback allocation failed");
    D3D12_RESOURCE_STATES prior = r->st;
    off_rt_transition(rt, D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION dst = {0}, src = {0};
    dst.pResource = rb; dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint.Footprint.Format = (DXGI_FORMAT)r->dxgi;
    dst.PlacedFootprint.Footprint.Width = r->w;
    dst.PlacedFootprint.Footprint.Height = r->h;
    dst.PlacedFootprint.Footprint.Depth = 1;
    dst.PlacedFootprint.Footprint.RowPitch = pitch;
    src.pResource = r->res; src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    s_d3d.cmd_list->lpVtbl->CopyTextureRegion(s_d3d.cmd_list, &dst, 0, 0, 0, &src, NULL);
    off_rt_transition(rt, prior);
    s_scene_timeline.copies[i].resource = rb;
    s_scene_timeline.copies[i].op = op;
    s_scene_timeline.copies[i].width = r->w;
    s_scene_timeline.copies[i].height = r->h;
    s_scene_timeline.copies[i].pitch = pitch;
    s_scene_timeline.copies[i].format = r->dxgi;
    s_scene_timeline.bytes += bytes;
    s_scene_timeline.queued++;
}

static void scene_timeline_finish(void)
{
    if (!s_scene_timeline.active) return;
    if (s_scene_timeline.queued != s_scene_timeline.count)
        scene_timeline_fail("requested operation was not submitted in the armed batch");
    if (!vp_upload_retire(NULL)) scene_timeline_fail("GPU retirement failed");
    for (u32 i = 0; i < s_scene_timeline.queued; i++) {
        char path[512];
        int length = snprintf(path, sizeof(path), "%s/timeline_frame_%llu_before_op_%u.bmp",
            getenv("RSX_CAP_DIR"), (unsigned long long)s_scene_timeline.frame,
            s_scene_timeline.copies[i].op);
        if (length < 0 || (size_t)length >= sizeof(path) ||
            GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES)
            scene_timeline_fail("invalid or already-existing output path");
        ID3D12Resource* rb = s_scene_timeline.copies[i].resource;
        D3D12_RANGE range = {0, (SIZE_T)s_scene_timeline.copies[i].pitch *
            s_scene_timeline.copies[i].height};
        void* data = NULL;
        if (FAILED(rb->lpVtbl->Map(rb, 0, &range, &data)) || !data)
            scene_timeline_fail("readback map failed");
        cap_write_bmp(path, data, s_scene_timeline.copies[i].width,
            s_scene_timeline.copies[i].height, s_scene_timeline.copies[i].pitch,
            s_scene_timeline.copies[i].format);
        /* BMP visualization discards alpha and clamps signed/HDR values.
         * Preserve the checked readback bytes for numeric shader diagnosis. */
        char raw_path[528];
        int raw_length = snprintf(raw_path, sizeof(raw_path), "%s.raw", path);
        if (raw_length < 0 || (size_t)raw_length >= sizeof(raw_path) ||
            GetFileAttributesA(raw_path) != INVALID_FILE_ATTRIBUTES)
            scene_timeline_fail("invalid or existing raw output path");
        FILE* raw = fopen(raw_path, "wb");
        if (!raw) scene_timeline_fail("raw output open failed");
        if (fwrite(data, 1, range.End, raw) != range.End)
            scene_timeline_fail("raw output write failed");
        if (fclose(raw)) scene_timeline_fail("raw output close failed");
        char layout_path[528];
        int layout_length = snprintf(layout_path, sizeof(layout_path), "%s.layout", path);
        if (layout_length < 0 || (size_t)layout_length >= sizeof(layout_path) ||
            GetFileAttributesA(layout_path) != INVALID_FILE_ATTRIBUTES)
            scene_timeline_fail("invalid or existing layout output path");
        FILE* layout = fopen(layout_path, "wb");
        if (!layout) scene_timeline_fail("layout output open failed");
        fprintf(layout, "target=%08X width=%u height=%u pitch=%u dxgi=%u bytes=%llu\n",
            s_scene_timeline.target, s_scene_timeline.copies[i].width,
            s_scene_timeline.copies[i].height, s_scene_timeline.copies[i].pitch,
            s_scene_timeline.copies[i].format, (unsigned long long)range.End);
        if (ferror(layout)) scene_timeline_fail("layout output write failed");
        if (fclose(layout)) scene_timeline_fail("layout output close failed");
        D3D12_RANGE written = {0, 0}; rb->lpVtbl->Unmap(rb, 0, &written);
        rb->lpVtbl->Release(rb); s_scene_timeline.copies[i].resource = NULL;
        fprintf(stderr, "[RSXTIMELINE] completed %s\n", path);
    }
    s_scene_timeline.active = 0; s_scene_timeline.done = 1;
}
#endif
