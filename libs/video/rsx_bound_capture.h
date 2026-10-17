/* Optional physical descriptor/source evidence. Payloads remain private.
 * Include after the timeline and address-table helpers, before srv_write_ex. */
#ifndef RSX_BOUND_CAPTURE_H
#define RSX_BOUND_CAPTURE_H
static struct {
    ID3D12Resource* res;
    UINT format, mapping;
    int cube, seen;
    u64 frame;
} s_bound_srv[SRV_HEAP_SIZE];
static unsigned s_bound_shader_id;
static u64 s_bound_file_bytes;
static int bound_enabled(void) { return getenv("RSX_TIMELINE_ARM_FILE") != NULL; }
static void bound_blob(const char* path, const void* data, size_t n)
{
    if (!n || s_bound_file_bytes+n>128ull*1024*1024 ||
        GetFileAttributesA(path)!=INVALID_FILE_ATTRIBUTES)
        scene_timeline_fail("binding output budget, empty data or existing file");
    FILE* f=fopen(path,"wb");
    if (!f || fwrite(data,1,n,f)!=n) scene_timeline_fail("binding output open/write failed");
    if (fclose(f)) scene_timeline_fail("binding output close failed");
    s_bound_file_bytes+=n;
}
static u32 scene_bound_shader(const char* source, ID3DBlob* binary, const D3D12_GRAPHICS_PIPELINE_STATE_DESC* pd)
{
    if (!bound_enabled()) return 0;
    const char* dir=getenv("RSX_CAP_DIR");
    if (!dir || !binary) scene_timeline_fail("binding capture directory/binary absent");
    if (!CreateDirectoryA(dir,NULL) && GetLastError()!=ERROR_ALREADY_EXISTS)
        scene_timeline_fail("binding capture directory failed");
    if (++s_bound_shader_id>65535) scene_timeline_fail("shader identity limit");
    char path[512];
    const char* suffix[]={"hlsl","dxbc","pso.bin","vs.dxbc"};
    for (unsigned i=0;i<4;i++) {
        int n=snprintf(path,sizeof(path),"%s/bound_shader_%u.%s",dir,s_bound_shader_id,suffix[i]);
        if(n<0 || (size_t)n>=sizeof(path)) scene_timeline_fail("shader path too long");
        const void* data=i==0?(const void*)source:i==1?binary->lpVtbl->GetBufferPointer(binary):i==2?(const void*)pd:pd->VS.pShaderBytecode;
        size_t len=i==0?strlen(source):i==1?binary->lpVtbl->GetBufferSize(binary):i==2?sizeof(*pd):pd->VS.BytecodeLength;
        bound_blob(path,data,len);
    }
    return s_bound_shader_id;
}
static void scene_bound_srv(u32 slot, ID3D12Resource* resource, UINT format, UINT mapping, int cube)
{
    if (!bound_enabled()) return;
    if(slot>=SRV_HEAP_SIZE) scene_timeline_fail("binding descriptor slot out of range");
    s_bound_srv[slot].res=resource;s_bound_srv[slot].format=format;
    s_bound_srv[slot].mapping=mapping;s_bound_srv[slot].cube=cube;
    s_bound_srv[slot].seen=1;s_bound_srv[slot].frame=s_d3d.frame_count;
}
typedef struct BoundInput {
    ID3D12Resource* readback;
    u64 bytes;
    char path[544];
} BoundInput;
static BoundInput s_bound_input[128];
static unsigned s_bound_input_n;
static u64 s_bound_input_bytes;
static void scene_bound_copy(ID3D12Resource* resource, D3D12_RESOURCE_STATES state, u32 op, unsigned u, UINT format, UINT mapping, const char* metadata, int output)
{
    if(!resource || s_bound_input_n>=128)scene_timeline_fail("copy resource/count invalid");
      D3D12_RESOURCE_DESC d;resource->lpVtbl->GetDesc(resource,&d);
      if(d.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D || d.Width>4096 || d.Height>4096 ||
         d.DepthOrArraySize!=1 || d.MipLevels!=1 || d.SampleDesc.Count!=1 ||
         ((output<2) && (d.Flags&D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)))
         scene_timeline_fail("input capture requires single-mip single-layer color 2D resource");
      const UINT subresource=output>=2 ? (UINT)(output-2) : 0;
      if(output>=2 && (output>3 || d.Format!=DXGI_FORMAT_D24_UNORM_S8_UINT))
        scene_timeline_fail("depth capture requires D24S8 plane zero or one");
      D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
      UINT rows=0;UINT64 row_bytes=0,bytes=0;
      s_d3d.device->lpVtbl->GetCopyableFootprints(s_d3d.device,&d,subresource,1,0,&footprint,&rows,&row_bytes,&bytes);
      if(!bytes || bytes+s_bound_input_bytes+s_bound_file_bytes>128ull*1024*1024)
        scene_timeline_fail("input capture budget exceeded");
      BoundInput* b=&s_bound_input[s_bound_input_n];
      int n=output>=2 ? snprintf(b->path,sizeof(b->path),"%s.depth_plane%u.raw",metadata,subresource) : output ? snprintf(b->path,sizeof(b->path),"%s.output.raw",metadata) : snprintf(b->path,sizeof(b->path),"%s.t%u.raw",metadata,u);
      if(n<0 || (size_t)n>=sizeof(b->path))scene_timeline_fail("input path too long");
      char layout[576];n=snprintf(layout,sizeof(layout),"%s.layout",b->path);
      if(n<0 || (size_t)n>=sizeof(layout) || GetFileAttributesA(layout)!=INVALID_FILE_ATTRIBUTES)
        scene_timeline_fail("input layout path invalid/existing");
      FILE* f=fopen(layout,"wb");if(!f)scene_timeline_fail("input layout open failed");
      fprintf(f,"op=%u unit=%u resource_dxgi=%u srv_dxgi=%u mapping=%08X width=%llu height=%u pitch=%u rows=%u row_bytes=%llu offset=%llu bytes=%llu prior_state=%u subresource=%u footprint_dxgi=%u\n",
        op,u,(unsigned)d.Format,format,mapping,
        (unsigned long long)d.Width,d.Height,footprint.Footprint.RowPitch,rows,
        (unsigned long long)row_bytes,(unsigned long long)footprint.Offset,
        (unsigned long long)bytes,(unsigned)state,subresource,(unsigned)footprint.Footprint.Format);
      if(ferror(f)||fclose(f))scene_timeline_fail("input layout write/close failed");
      D3D12_HEAP_PROPERTIES hp={0};hp.Type=D3D12_HEAP_TYPE_READBACK;
      D3D12_RESOURCE_DESC bd={0};bd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;bd.Width=bytes;
      bd.Height=1;bd.DepthOrArraySize=1;bd.MipLevels=1;bd.SampleDesc.Count=1;bd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
      HRESULT hr=s_d3d.device->lpVtbl->CreateCommittedResource(s_d3d.device,&hp,D3D12_HEAP_FLAG_NONE,
        &bd,D3D12_RESOURCE_STATE_COPY_DEST,NULL,&IID_ID3D12Resource,(void**)&b->readback);
      if(FAILED(hr)||!b->readback)scene_timeline_fail("input readback allocation failed");
      D3D12_RESOURCE_BARRIER barrier={0};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
      barrier.Transition.pResource=resource;barrier.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
      barrier.Transition.StateBefore=state;barrier.Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_SOURCE;
      if(state!=D3D12_RESOURCE_STATE_COPY_SOURCE)s_d3d.cmd_list->lpVtbl->ResourceBarrier(s_d3d.cmd_list,1,&barrier);
      D3D12_TEXTURE_COPY_LOCATION src={0},dst={0};src.pResource=resource;src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;src.SubresourceIndex=subresource;
      dst.pResource=b->readback;dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=footprint;
      s_d3d.cmd_list->lpVtbl->CopyTextureRegion(s_d3d.cmd_list,&dst,0,0,0,&src,NULL);
      if(state!=D3D12_RESOURCE_STATE_COPY_SOURCE){barrier.Transition.StateBefore=D3D12_RESOURCE_STATE_COPY_SOURCE;
        barrier.Transition.StateAfter=state;s_d3d.cmd_list->lpVtbl->ResourceBarrier(s_d3d.cmd_list,1,&barrier);}
      b->bytes=bytes;s_bound_input_bytes+=bytes;s_bound_input_n++;
}
static void scene_bound_inputs(u32 op, u32 base, unsigned units, const char* metadata)
{
    const char* mask_text=getenv("RSX_BOUND_INPUT_MASK");
    if(!mask_text)return;
    u32 mask=0;
    size_t len=strlen(mask_text);
    if(!len || len>4)scene_timeline_fail("input mask must be 1..FFFF hexadecimal");
    for(size_t i=0;i<len;i++){
      unsigned c=(unsigned char)mask_text[i],d;
      if(c>='0'&&c<='9')d=c-'0';else if(c>='a'&&c<='f')d=c-'a'+10;
      else if(c>='A'&&c<='F')d=c-'A'+10;else {scene_timeline_fail("invalid input mask");return;}
      mask=(mask<<4)|d;
    }
    if(!mask || (units<16 && (mask>>units)))scene_timeline_fail("input mask outside bound unit window");
    for(unsigned u=0;u<units;u++)if(mask&(1u<<u)){
      ID3D12Resource* resource=s_bound_srv[base+u].res;
      if(!resource || s_bound_input_n>=128)scene_timeline_fail("input resource absent or count exceeded");
      int known=0;
      D3D12_RESOURCE_STATES state=D3D12_RESOURCE_STATE_COMMON;
      for(int rt=0;rt<RSX_BOUND_OFFRT_COUNT;rt++)if(s_d3d.off_rt[rt].res==resource){
        known=1;state=s_d3d.off_rt[rt].st;break;
      }
      if(!known)for(int t=0;t<VP_TEX_SLOTS;t++)if(s_d3d.vp_tex[t].res==resource){
        known=1;state=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;break;
      }
      #ifdef RSX_BOUND_INPUT_STATE
      if(!known)known=RSX_BOUND_INPUT_STATE(resource,&state);
      #endif
      if(!known)scene_timeline_fail("input resource state provenance unavailable");
      scene_bound_copy(resource,state,op,u,s_bound_srv[base+u].format,s_bound_srv[base+u].mapping,metadata,0);
    }
}
static u32 s_bound_output_op;
static char s_bound_output_metadata[512];
static int s_bound_output_pending;
static void scene_bound_after(u32 op, ID3D12Resource* resource, D3D12_RESOURCE_STATES state)
{
    if(!s_bound_output_pending || op!=s_bound_output_op)return;
    s_bound_output_pending=0;
    D3D12_RESOURCE_DESC desc;resource->lpVtbl->GetDesc(resource,&desc);
    scene_bound_copy(resource,state,op,0xFFFFFFFFu,(UINT)desc.Format,
      D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,s_bound_output_metadata,1);
}
static void scene_bound_finish(void)
{
    if(!s_bound_input_n)return;
    if(!vp_upload_retire(NULL))scene_timeline_fail("input GPU retirement failed");
    for(unsigned i=0;i<s_bound_input_n;i++){
      BoundInput* b=&s_bound_input[i];void* data=NULL;
      D3D12_RANGE read={0,(SIZE_T)b->bytes};
      HRESULT hr=b->readback->lpVtbl->Map(b->readback,0,&read,&data);
      if(FAILED(hr)||!data)scene_timeline_fail("input readback map failed");
      bound_blob(b->path,data,(size_t)b->bytes);
      D3D12_RANGE written={0,0};b->readback->lpVtbl->Unmap(b->readback,0,&written);
      b->readback->lpVtbl->Release(b->readback);b->readback=NULL;
    }
    s_bound_input_n=0;
}
static void scene_bound_draw(u32 op, u32 base, unsigned units, ID3D12PipelineState* pso)
{
    if(!s_scene_timeline.active) return;
    unsigned i=0;for(;i<s_scene_timeline.count;i++) if(s_scene_timeline.ops[i]==op)break;
    if(i==s_scene_timeline.count)return;
    if(units>16 || base+units>SRV_HEAP_SIZE || !s_address_observed_table)
        scene_timeline_fail("bound descriptor table absent or out of range");
    D3D12DrawRecord* dr=&s_d3d.draws[op];
    if(dr->cb_slot>=MAX_DRAWS || !s_d3d.vp_cb_mapped || !s_d3d.vp_fpcb_mapped)
        scene_timeline_fail("served constant slot absent");
    char path[512];int n=snprintf(path,sizeof(path),"%s/timeline_frame_%llu_before_op_%u_bindings.txt",
        getenv("RSX_CAP_DIR"),(unsigned long long)s_scene_timeline.frame,op);
    if(n<0 || (size_t)n>=sizeof(path) || GetFileAttributesA(path)!=INVALID_FILE_ATTRIBUTES)
        scene_timeline_fail("bound metadata path invalid/existing");
    FILE* f=fopen(path,"wb");if(!f)scene_timeline_fail("bound metadata open failed");
    int found=0;
    for(int q=0;q<s_d3d.vp_fp_n;q++)if(s_d3d.vp_fp[q].pso==pso){
        fprintf(f,"shader_id=%u sampler_mask=%08X alias_key=%016llX blend=%08X cmask=%04X\n",
          s_d3d.vp_fp[q].diag_shader_id,s_d3d.vp_fp[q].sampler_mask,
          (unsigned long long)s_d3d.vp_fp[q].alias_key,s_d3d.vp_fp[q].blend,s_d3d.vp_fp[q].cmask);
        found=1;break;
    }
    if(!found)fprintf(f,"shader_source=unavailable active_pso=%p\n",(void*)pso);
    fprintf(f,"srv_base=%u units=%u parity=%d cb_slot=%u\n",base,units,s_d3d.vp_parity,dr->cb_slot);
    for(unsigned u=0;u<units;u++){
        unsigned k=base+u;
        if(!s_bound_srv[k].seen)scene_timeline_fail("bound descriptor was not observed");
        D3D12_RESOURCE_DESC d={0};
        if(s_bound_srv[k].res)s_bound_srv[k].res->lpVtbl->GetDesc(s_bound_srv[k].res,&d);
        int off=-1;for(int rt=0;rt<RSX_BOUND_OFFRT_COUNT;rt++)
          if(s_d3d.off_rt[rt].res==s_bound_srv[k].res && s_bound_srv[k].res){off=rt;break;}
        const D3D12_SAMPLER_DESC* a=&s_address_observed_table[u];
        fprintf(f,"t%u resource=%p srv_dxgi=%u mapping=%08X cube=%d written_frame=%llu resource_dxgi=%u width=%llu height=%u array=%u mips=%u offrt_slot=%d offrt_offset=%08X filter=%u uvw=%u/%u/%u bias=%.9g lod=%.9g/%.9g aniso=%u border=%.9g/%.9g/%.9g/%.9g\n",
          u,(void*)s_bound_srv[k].res,s_bound_srv[k].format,s_bound_srv[k].mapping,s_bound_srv[k].cube,
          (unsigned long long)s_bound_srv[k].frame,(unsigned)d.Format,(unsigned long long)d.Width,
          d.Height,d.DepthOrArraySize,d.MipLevels,off,off<0?0:s_d3d.off_rt[off].off,
          (unsigned)a->Filter,(unsigned)a->AddressU,(unsigned)a->AddressV,(unsigned)a->AddressW,
          a->MipLODBias,a->MinLOD,a->MaxLOD,a->MaxAnisotropy,
          a->BorderColor[0],a->BorderColor[1],a->BorderColor[2],a->BorderColor[3]);
    }
    fprintf(f,"vertices=%u vb_offset=%u stride=256 topology=%u\n",dr->vertex_count,dr->vb_byte_offset,(unsigned)dr->topology);
    if(ferror(f) || fclose(f))scene_timeline_fail("bound metadata write/close failed");
    scene_bound_inputs(op,base,units,path);
#ifdef RSX_BOUND_DEPTH_RESOURCE
    if(getenv("RSX_BOUND_INPUT_MASK")){
      ID3D12Resource* depth=RSX_BOUND_DEPTH_RESOURCE(dr);
      for(unsigned plane=0;plane<2;plane++)
        scene_bound_copy(depth,D3D12_RESOURCE_STATE_DEPTH_WRITE,op,0xFFFFFFFEu,
          DXGI_FORMAT_D24_UNORM_S8_UINT,0,path,2+(int)plane);
    }
#endif
    if(getenv("RSX_BOUND_INPUT_MASK")){
      if(s_bound_output_pending)scene_timeline_fail("selected output was not copied");
      memcpy(s_bound_output_metadata,path,strlen(path)+1);
      s_bound_output_op=op;s_bound_output_pending=1;
    }
    const u64 vb_bytes=(u64)dr->vertex_count*256;
    if(!s_d3d.vp_vb_mapped || !vb_bytes || (u64)dr->vb_byte_offset+vb_bytes>(u64)s_d3d.vp_capacity*256)
      scene_timeline_fail("served vertex range invalid");
    char vb_path[544];n=snprintf(vb_path,sizeof(vb_path),"%s.served_vb.bin",path);
    if(n<0 || (size_t)n>=sizeof(vb_path))scene_timeline_fail("vertex payload path too long");
    bound_blob(vb_path,(const char*)s_d3d.vp_vb_mapped+(u64)s_d3d.vp_parity*s_d3d.vp_capacity*256+dr->vb_byte_offset,(size_t)vb_bytes);
    const size_t slot=(size_t)s_d3d.vp_parity*MAX_DRAWS+dr->cb_slot;
    const char* suffix[]={"served_b0.bin","served_b1.bin","vs.dxbc"};
    for(unsigned k=0;k<3;k++){
      char data_path[544];n=snprintf(data_path,sizeof(data_path),"%s.%s",path,suffix[k]);
      if(n<0 || (size_t)n>=sizeof(data_path))scene_timeline_fail("bound payload path too long");
      const void* data;size_t bytes;
      if(k==0){data=(const char*)s_d3d.vp_cb_mapped+slot*VP_CB_STRIDE;bytes=VP_CB_STRIDE;}
      else if(k==1){data=(const char*)s_d3d.vp_fpcb_mapped+slot*VP_FPCB_STRIDE;bytes=VP_FPCB_STRIDE;}
      else {
        ID3DBlob* b=dr->vs_idx>=0 && dr->vs_idx<VP_VS_CACHE ? s_d3d.vp_vs[dr->vs_idx].vs : NULL;
        if(!b)scene_timeline_fail("bound vertex shader binary unavailable");
        data=b->lpVtbl->GetBufferPointer(b);bytes=b->lpVtbl->GetBufferSize(b);
      }
      bound_blob(data_path,data,bytes);
    }
}
#endif
