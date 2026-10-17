/* D24S8 memory sampled as A8R8G8B8. No guest program/address special cases.
 * Prepare after GPU retirement, before recording. Source stays DEPTH_WRITE.
 * Raw plane copies preserve the stored 24 bits; no float requantization. */
#ifndef RSX_D3D12_DEPTH_ALIAS_H
#define RSX_D3D12_DEPTH_ALIAS_H
#include <d3d12.h>
#include <d3dcompiler.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define RSX_DEPTH_ALIAS_CAPACITY 32
static const char rsx_depth_alias_hlsl[] =
 "ByteAddressBuffer source:register(t0); RWTexture2D<float4> target:register(u0);"
 "cbuffer Layout:register(b0){uint depth_pitch;uint stencil_offset;uint stencil_pitch;}"
 "[numthreads(8,8,1)] void main(uint3 p:SV_DispatchThreadID){"
 "uint w,h;target.GetDimensions(w,h);if(p.x>=w||p.y>=h)return;"
 "uint z=source.Load(p.y*depth_pitch+p.x*4)&0xffffff;"
 "uint a=stencil_offset+p.y*stencil_pitch+p.x;"
 "uint s=(source.Load(a&~3u)>>((a&3)*8))&255;"
 "target[p.xy]=float4((z>>8)&255,z&255,s,(z>>16)&255)/255.;}";
typedef struct rsx_depth_alias_view {
 ID3D12Resource *source, *planes, *color;
 D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint[2];
 uint32_t width,height,layout[3];
 uint64_t bytes,generation;
} rsx_depth_alias_view;
typedef struct rsx_depth_alias_cache {
 ID3D12RootSignature* root;
 ID3D12PipelineState* pipeline;
 ID3D12DescriptorHeap* heap;
 UINT step,count;
 uint64_t bytes;
 rsx_depth_alias_view views[RSX_DEPTH_ALIAS_CAPACITY];
} rsx_depth_alias_cache;
static HRESULT rsx_depth_alias_init(rsx_depth_alias_cache* c,ID3D12Device* device)
{
 if(c->pipeline)return S_OK;
 D3D12_DESCRIPTOR_HEAP_DESC hd={0};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
 hd.NumDescriptors=RSX_DEPTH_ALIAS_CAPACITY*2;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
 HRESULT hr=device->lpVtbl->CreateDescriptorHeap(device,&hd,&IID_ID3D12DescriptorHeap,(void**)&c->heap);
 if(FAILED(hr))return hr;
 c->step=device->lpVtbl->GetDescriptorHandleIncrementSize(device,hd.Type);
 D3D12_DESCRIPTOR_RANGE ranges[2]={{0}};ranges[0].RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
 ranges[0].NumDescriptors=1;ranges[1].RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
 ranges[1].NumDescriptors=1;ranges[1].OffsetInDescriptorsFromTableStart=1;
 D3D12_ROOT_PARAMETER params[2]={{0}};
 params[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
 params[0].DescriptorTable.NumDescriptorRanges=2;params[0].DescriptorTable.pDescriptorRanges=ranges;
 params[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;params[1].Constants.Num32BitValues=3;
 D3D12_ROOT_SIGNATURE_DESC rd={0};rd.NumParameters=2;rd.pParameters=params;
 ID3DBlob *root=NULL,*code=NULL,*errors=NULL;
 hr=D3D12SerializeRootSignature(&rd,D3D_ROOT_SIGNATURE_VERSION_1,&root,&errors);
 if(errors){fprintf(stderr,"[D24ALIAS] %.*s\n",(int)errors->lpVtbl->GetBufferSize(errors),
   (const char*)errors->lpVtbl->GetBufferPointer(errors));errors->lpVtbl->Release(errors);errors=NULL;}
 if(FAILED(hr))return hr;
 hr=device->lpVtbl->CreateRootSignature(device,0,root->lpVtbl->GetBufferPointer(root),
   root->lpVtbl->GetBufferSize(root),&IID_ID3D12RootSignature,(void**)&c->root);
 root->lpVtbl->Release(root);if(FAILED(hr))return hr;
 hr=D3DCompile(rsx_depth_alias_hlsl,sizeof(rsx_depth_alias_hlsl)-1,NULL,NULL,NULL,
   "main","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&errors);
 if(errors){fprintf(stderr,"[D24ALIAS] %.*s\n",(int)errors->lpVtbl->GetBufferSize(errors),
   (const char*)errors->lpVtbl->GetBufferPointer(errors));errors->lpVtbl->Release(errors);errors=NULL;}if(FAILED(hr))return hr;
 D3D12_COMPUTE_PIPELINE_STATE_DESC pd={0};pd.pRootSignature=c->root;
 pd.CS.pShaderBytecode=code->lpVtbl->GetBufferPointer(code);pd.CS.BytecodeLength=code->lpVtbl->GetBufferSize(code);
 hr=device->lpVtbl->CreateComputePipelineState(device,&pd,&IID_ID3D12PipelineState,(void**)&c->pipeline);
 code->lpVtbl->Release(code);return hr;
}
static HRESULT rsx_depth_alias_prepare(rsx_depth_alias_cache* c,ID3D12Device* device,
 ID3D12Resource* source,UINT w,UINT h,int* index)
{
 if(!source||!w||!h||w>4096||h>4096)return E_INVALIDARG;
 for(UINT i=0;i<c->count;i++)if(c->views[i].source==source&&c->views[i].width==w&&c->views[i].height==h){*index=(int)i;return S_OK;}
 if(c->count==RSX_DEPTH_ALIAS_CAPACITY)return E_OUTOFMEMORY;
 D3D12_RESOURCE_DESC src;source->lpVtbl->GetDesc(source,&src);
 if(src.Format!=DXGI_FORMAT_D24_UNORM_S8_UINT||src.MipLevels!=1||src.DepthOrArraySize!=1||
   src.SampleDesc.Count!=1||w>src.Width||h>src.Height)return E_INVALIDARG;
 HRESULT hr=rsx_depth_alias_init(c,device);if(FAILED(hr))return hr;
 rsx_depth_alias_view v={0};UINT64 bytes[2];UINT rows;UINT64 row_bytes;
 device->lpVtbl->GetCopyableFootprints(device,&src,0,1,0,&v.footprint[0],&rows,&row_bytes,&bytes[0]);
 device->lpVtbl->GetCopyableFootprints(device,&src,1,1,(bytes[0]+511)&~511ull,&v.footprint[1],&rows,&row_bytes,&bytes[1]);
 v.bytes=v.footprint[1].Offset+bytes[1]+(uint64_t)w*h*4;
 if(v.bytes+c->bytes>256ull*1024*1024)return E_OUTOFMEMORY;
 D3D12_HEAP_PROPERTIES hp={0};hp.Type=D3D12_HEAP_TYPE_DEFAULT;
 D3D12_RESOURCE_DESC bd={0};bd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;
 bd.Width=v.footprint[1].Offset+bytes[1];bd.Height=1;bd.DepthOrArraySize=1;
 bd.MipLevels=1;bd.SampleDesc.Count=1;bd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
 hr=device->lpVtbl->CreateCommittedResource(device,&hp,D3D12_HEAP_FLAG_NONE,&bd,
  D3D12_RESOURCE_STATE_COPY_DEST,NULL,&IID_ID3D12Resource,(void**)&v.planes);if(FAILED(hr))return hr;
 D3D12_RESOURCE_DESC td={0};td.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;td.Width=w;td.Height=h;
 td.DepthOrArraySize=1;td.MipLevels=1;td.SampleDesc.Count=1;td.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
 td.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
 hr=device->lpVtbl->CreateCommittedResource(device,&hp,D3D12_HEAP_FLAG_NONE,&td,
  D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,NULL,&IID_ID3D12Resource,(void**)&v.color);
 if(FAILED(hr)){v.planes->lpVtbl->Release(v.planes);return hr;}
 D3D12_CPU_DESCRIPTOR_HANDLE cpu;c->heap->lpVtbl->GetCPUDescriptorHandleForHeapStart(c->heap,&cpu);
 cpu.ptr+=(SIZE_T)c->count*2*c->step;
 D3D12_SHADER_RESOURCE_VIEW_DESC sv={0};sv.Format=DXGI_FORMAT_R32_TYPELESS;
 sv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;sv.ViewDimension=D3D12_SRV_DIMENSION_BUFFER;
 sv.Buffer.NumElements=(UINT)(bd.Width/4);sv.Buffer.Flags=D3D12_BUFFER_SRV_FLAG_RAW;
 device->lpVtbl->CreateShaderResourceView(device,v.planes,&sv,cpu);cpu.ptr+=c->step;
 D3D12_UNORDERED_ACCESS_VIEW_DESC uv={0};uv.Format=td.Format;uv.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;
 device->lpVtbl->CreateUnorderedAccessView(device,v.color,NULL,&uv,cpu);
 v.source=source;source->lpVtbl->AddRef(source);v.width=w;v.height=h;
 v.layout[0]=v.footprint[0].Footprint.RowPitch;v.layout[1]=(uint32_t)v.footprint[1].Offset;
 v.layout[2]=v.footprint[1].Footprint.RowPitch;*index=(int)c->count;
 c->views[c->count++]=v;c->bytes+=v.bytes;return S_OK;
}
static void rsx_depth_alias_barrier(ID3D12GraphicsCommandList* cmd,ID3D12Resource* r,
 D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after)
{
 D3D12_RESOURCE_BARRIER b={0};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
 b.Transition.pResource=r;b.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
 b.Transition.StateBefore=before;b.Transition.StateAfter=after;cmd->lpVtbl->ResourceBarrier(cmd,1,&b);
}
/* Changes compute PSO and descriptor heaps. Caller must restore graphics PSO
 * and invalidate both cached graphics descriptor tables before another draw. */
static int rsx_depth_alias_refresh(rsx_depth_alias_cache* c,int index,
 ID3D12GraphicsCommandList* cmd,uint64_t generation)
{
 rsx_depth_alias_view* v=&c->views[index];
 if(v->generation==generation&&generation)return 0;
 rsx_depth_alias_barrier(cmd,v->source,D3D12_RESOURCE_STATE_DEPTH_WRITE,D3D12_RESOURCE_STATE_COPY_SOURCE);
 for(UINT p=0;p<2;p++){
  D3D12_TEXTURE_COPY_LOCATION src={0},dst={0};src.pResource=v->source;
  src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;src.SubresourceIndex=p;
  dst.pResource=v->planes;dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=v->footprint[p];
  cmd->lpVtbl->CopyTextureRegion(cmd,&dst,0,0,0,&src,NULL);
 }
 rsx_depth_alias_barrier(cmd,v->source,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_DEPTH_WRITE);
 rsx_depth_alias_barrier(cmd,v->planes,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
 rsx_depth_alias_barrier(cmd,v->color,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
 cmd->lpVtbl->SetDescriptorHeaps(cmd,1,&c->heap);cmd->lpVtbl->SetComputeRootSignature(cmd,c->root);
 cmd->lpVtbl->SetPipelineState(cmd,c->pipeline);
 D3D12_GPU_DESCRIPTOR_HANDLE gpu;c->heap->lpVtbl->GetGPUDescriptorHandleForHeapStart(c->heap,&gpu);
 gpu.ptr+=(UINT64)index*2*c->step;cmd->lpVtbl->SetComputeRootDescriptorTable(cmd,0,gpu);
 cmd->lpVtbl->SetComputeRoot32BitConstants(cmd,1,3,v->layout,0);
 cmd->lpVtbl->Dispatch(cmd,(v->width+7)/8,(v->height+7)/8,1);
 rsx_depth_alias_barrier(cmd,v->planes,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_DEST);
 rsx_depth_alias_barrier(cmd,v->color,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
 v->generation=generation;return 1;
}
static void rsx_depth_alias_release(rsx_depth_alias_cache* c)
{
 for(UINT i=0;i<c->count;i++){
  c->views[i].source->lpVtbl->Release(c->views[i].source);
  c->views[i].planes->lpVtbl->Release(c->views[i].planes);
  c->views[i].color->lpVtbl->Release(c->views[i].color);
 }
 if(c->pipeline)c->pipeline->lpVtbl->Release(c->pipeline);
 if(c->root)c->root->lpVtbl->Release(c->root);
 if(c->heap)c->heap->lpVtbl->Release(c->heap);memset(c,0,sizeof(*c));
}
#endif
