/* Authored headless DX12 functional fixture. See tools/test_rsx_texture_gpu.py. */

#include "backend-static.c"
#include <crtdbg.h>
uint8_t* vm_base;
/* Link-only diagnostic boundary: no VRAM relocation is part of this fixture. */
u32 rsx_find_vram_upload(u32 bytes){(void)bytes;fprintf(stderr,"UNEXPECTED VRAM relocation boundary\n");abort();}
static void gpu_errors(void){
 if(!s_d3d.device)return;
 fprintf(stderr,"Device reason=%08lX\n",(unsigned long)s_d3d.device->lpVtbl->GetDeviceRemovedReason(s_d3d.device));
 ID3D12InfoQueue*q=NULL;if(FAILED(s_d3d.device->lpVtbl->QueryInterface(s_d3d.device,&IID_ID3D12InfoQueue,(void**)&q)))return;
 uint64_t count=q->lpVtbl->GetNumStoredMessagesAllowedByRetrievalFilter(q);
 for(uint64_t i=0;i<count;i++){SIZE_T n=0;q->lpVtbl->GetMessage(q,i,NULL,&n);D3D12_MESSAGE*m=malloc(n);if(m&&SUCCEEDED(q->lpVtbl->GetMessage(q,i,m,&n)))fprintf(stderr,"D3D12 severity=%u: %s\n",m->Severity,m->pDescription);free(m);}q->lpVtbl->Release(q);
}
#define REQUIRE(c) do{if(!(c)){fprintf(stderr,"GPU contract failed line %d: %s\n",__LINE__,#c);gpu_errors();exit(7);}}while(0)
#define HR(c) do{HRESULT h=(c);if(FAILED(h)){fprintf(stderr,"GPU HRESULT line %d: %08lX\n",__LINE__,(unsigned long)h);exit(8);}}while(0)
static ID3D12Resource* rb;
static D3D12_PLACED_SUBRESOURCE_FOOTPRINT rb_fp;
static uint64_t rb_size;
static void reset_commands(void){HR(s_d3d.cmd_allocators[0]->lpVtbl->Reset(s_d3d.cmd_allocators[0]));HR(s_d3d.cmd_list->lpVtbl->Reset(s_d3d.cmd_list,s_d3d.cmd_allocators[0],NULL));}
static void submit(void){HR(s_d3d.cmd_list->lpVtbl->Close(s_d3d.cmd_list));ID3D12CommandList*l=(ID3D12CommandList*)s_d3d.cmd_list;s_d3d.cmd_queue->lpVtbl->ExecuteCommandLists(s_d3d.cmd_queue,1,&l);}
static void capture_face(int slot,unsigned face){
 ID3D12Resource*r=s_d3d.vp_tex[slot].res;
 D3D12_RESOURCE_DESC td;r->lpVtbl->GetDesc(r,&td);
 REQUIRE(td.Format==DXGI_FORMAT_R8G8B8A8_UNORM);
 if(!rb){
  s_d3d.device->lpVtbl->GetCopyableFootprints(s_d3d.device,&td,0,1,0,&rb_fp,NULL,NULL,&rb_size);
  D3D12_HEAP_PROPERTIES hp={0};hp.Type=D3D12_HEAP_TYPE_READBACK;
  D3D12_RESOURCE_DESC d={0};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=rb_size;d.Height=1;d.DepthOrArraySize=1;d.MipLevels=1;d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  HR(s_d3d.device->lpVtbl->CreateCommittedResource(s_d3d.device,&hp,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_COPY_DEST,NULL,&IID_ID3D12Resource,(void**)&rb));
 }
 D3D12_RESOURCE_BARRIER b={0};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition.pResource=r;b.Transition.StateBefore=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;b.Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_SOURCE;b.Transition.Subresource=face;
 s_d3d.cmd_list->lpVtbl->ResourceBarrier(s_d3d.cmd_list,1,&b);
 D3D12_TEXTURE_COPY_LOCATION dst={0},src={0};dst.pResource=rb;dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=rb_fp;src.pResource=r;src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;src.SubresourceIndex=face;
 s_d3d.cmd_list->lpVtbl->CopyTextureRegion(s_d3d.cmd_list,&dst,0,0,0,&src,NULL);
 b.Transition.StateBefore=D3D12_RESOURCE_STATE_COPY_SOURCE;b.Transition.StateAfter=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;s_d3d.cmd_list->lpVtbl->ResourceBarrier(s_d3d.cmd_list,1,&b);
}
static void capture(int slot){capture_face(slot,0);}
static void pixels_argb(const uint8_t* argb){
 void*p=NULL;D3D12_RANGE range={0,(SIZE_T)rb_size};HR(rb->lpVtbl->Map(rb,0,&range,&p));
 for(unsigned y=0;y<128;y++)for(unsigned x=0;x<128;x++){
  const uint8_t*src=argb+(y*128+x)*4;uint8_t*v=(uint8_t*)p+y*rb_fp.Footprint.RowPitch+x*4;
  REQUIRE(v[0]==src[1]&&v[1]==src[2]&&v[2]==src[3]&&v[3]==src[0]);
 }
 D3D12_RANGE wr={0,0};rb->lpVtbl->Unmap(rb,0,&wr);
}
static void pixels(int r,int g,int b){
 void*p=NULL;D3D12_RANGE range={0,(SIZE_T)rb_size};HR(rb->lpVtbl->Map(rb,0,&range,&p));
 for(unsigned y=0;y<128;y++)for(unsigned x=0;x<128;x++){
  uint8_t*v=(uint8_t*)p+y*rb_fp.Footprint.RowPitch+x*4;
  REQUIRE(v[0]==r&&v[1]==g&&v[2]==b&&v[3]==255);
 }
 D3D12_RANGE wr={0,0};rb->lpVtbl->Unmap(rb,0,&wr);
}
static int upload(unsigned id){return vp_upload_tex_slot_impl(0x1000+id*0x10000,128,128,0xa5,0,1,NULL);}
static DWORD WINAPI release_gate(void* value){
 Sleep(500);ID3D12Fence*gate=value;return FAILED(gate->lpVtbl->Signal(gate,1))?1:0;
}
int main(int argc,char**argv){
 _set_error_mode(_OUT_TO_STDERR);_set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);
 ID3D12Debug*debug=NULL;HR(D3D12GetDebugInterface(&IID_ID3D12Debug,(void**)&debug));debug->lpVtbl->EnableDebugLayer(debug);debug->lpVtbl->Release(debug);
 const char*choice=getenv("RSX_TEST_ADAPTER");int warp=choice&&!strcmp(choice,"warp");IDXGIAdapter*chosen=NULL;
 if(warp){IDXGIFactory4*f=NULL;HR(CreateDXGIFactory1(&IID_IDXGIFactory4,(void**)&f));HR(f->lpVtbl->EnumWarpAdapter(f,&IID_IDXGIAdapter,(void**)&chosen));f->lpVtbl->Release(f);}
 HR(D3D12CreateDevice(chosen,D3D_FEATURE_LEVEL_11_0,&IID_ID3D12Device,(void**)&s_d3d.device));if(chosen)chosen->lpVtbl->Release(chosen);
 LUID luid;s_d3d.device->lpVtbl->GetAdapterLuid(s_d3d.device,&luid);
 IDXGIFactory4*factory=NULL;IDXGIAdapter1*adapter=NULL;DXGI_ADAPTER_DESC1 ad;
 HR(CreateDXGIFactory1(&IID_IDXGIFactory4,(void**)&factory));
 HR(factory->lpVtbl->EnumAdapterByLuid(factory,luid,&IID_IDXGIAdapter1,(void**)&adapter));HR(adapter->lpVtbl->GetDesc1(adapter,&ad));
 printf("Adapter vendor=%04X device=%04X flags=%u name=%ls\n",ad.VendorId,ad.DeviceId,ad.Flags,ad.Description);
 REQUIRE(warp?!!(ad.Flags&DXGI_ADAPTER_FLAG_SOFTWARE):!(ad.Flags&DXGI_ADAPTER_FLAG_SOFTWARE));adapter->lpVtbl->Release(adapter);factory->lpVtbl->Release(factory);
 ID3D12InfoQueue*info=NULL;HR(s_d3d.device->lpVtbl->QueryInterface(s_d3d.device,&IID_ID3D12InfoQueue,(void**)&info));
 D3D12_COMMAND_QUEUE_DESC q={0};q.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
 HR(s_d3d.device->lpVtbl->CreateCommandQueue(s_d3d.device,&q,&IID_ID3D12CommandQueue,(void**)&s_d3d.cmd_queue));
 HR(s_d3d.device->lpVtbl->CreateCommandAllocator(s_d3d.device,D3D12_COMMAND_LIST_TYPE_DIRECT,&IID_ID3D12CommandAllocator,(void**)&s_d3d.cmd_allocators[0]));
 HR(s_d3d.device->lpVtbl->CreateCommandList(s_d3d.device,0,D3D12_COMMAND_LIST_TYPE_DIRECT,s_d3d.cmd_allocators[0],NULL,&IID_ID3D12GraphicsCommandList,(void**)&s_d3d.cmd_list));
 HR(s_d3d.device->lpVtbl->CreateFence(s_d3d.device,0,D3D12_FENCE_FLAG_NONE,&IID_ID3D12Fence,(void**)&s_d3d.fence));s_d3d.fence_event=CreateEvent(NULL,FALSE,FALSE,NULL);REQUIRE(s_d3d.fence_event);
 D3D12_DESCRIPTOR_HEAP_DESC hd={0};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;hd.NumDescriptors=10;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
 HR(s_d3d.device->lpVtbl->CreateDescriptorHeap(s_d3d.device,&hd,&IID_ID3D12DescriptorHeap,(void**)&s_d3d.srv_heap));
 s_d3d.vp_tex_capacity=10;s_d3d.vp_tex=calloc(10,sizeof(VPTexSlot));REQUIRE(s_d3d.vp_tex);
 vm_base=calloc(1,2*1024*1024);REQUIRE(vm_base);
 for(unsigned id=0;id<10;id++)for(unsigned j=0;j<128*128;j++){
  uint8_t*v=vm_base+0x1000+id*0x10000+j*4;v[0]=255;v[1]=id%3==0?255:0;v[2]=id%3==1?255:0;v[3]=id%3==2?255:0;
 }
 REQUIRE(texture_retention_budget()==1024*1024);
 ID3D12Fence*gate=NULL;
 if(argc>1) HR(s_d3d.cmd_list->lpVtbl->Close(s_d3d.cmd_list));
 if(argc>1&&!strcmp(argv[1],"stale-wake-only")){
  HR(s_d3d.device->lpVtbl->CreateFence(s_d3d.device,0,D3D12_FENCE_FLAG_NONE,&IID_ID3D12Fence,(void**)&gate));
  HR(s_d3d.cmd_queue->lpVtbl->Wait(s_d3d.cmd_queue,gate,1));
  REQUIRE(SetEvent(s_d3d.fence_event)); /* stale notification, not GPU completion */
  HANDLE worker=CreateThread(NULL,0,release_gate,gate,0,NULL);REQUIRE(worker);
  int retired_ok=gpu_pso_retire();REQUIRE(WaitForSingleObject(worker,2000)==WAIT_OBJECT_0);
  DWORD code=1;REQUIRE(GetExitCodeThread(worker,&code));CloseHandle(worker);REQUIRE(code==0);REQUIRE(retired_ok);
  REQUIRE(s_d3d.fence->lpVtbl->GetCompletedValue(s_d3d.fence)>=s_d3d.fence_values[0]);
  goto finish;
 }

 if(argc==1){
 int a=upload(0),b=upload(1);fprintf(stderr,"slots A=%d B=%d bytes=%llu\n",a,b,(unsigned long long)s_d3d.vp_tex_bytes);REQUIRE(a==0&&b==1);ID3D12Resource*ra=s_d3d.vp_tex[a].res,*rbefore=s_d3d.vp_tex[b].res;
 capture(a);submit();REQUIRE(vp_upload_retire(NULL));pixels(255,0,0);
 address_retire();texture_retention_begin_batch();reset_commands();
 int c=upload(2);REQUIRE(c==2&&s_d3d.vp_tex[a].res==ra&&s_d3d.vp_tex[b].res==rbefore);
 REQUIRE(upload(0)==a&&upload(1)==b);
 for(unsigned id=3;id<10;id++)REQUIRE(upload(id)==(int)id);
 REQUIRE(s_d3d.vp_tex_bytes>texture_retention_budget());REQUIRE(s_d3d.vp_tex_peak_over_budget==s_d3d.vp_tex_bytes-texture_retention_budget());
 capture(c);
 HR(s_d3d.device->lpVtbl->CreateFence(s_d3d.device,0,D3D12_FENCE_FLAG_NONE,&IID_ID3D12Fence,(void**)&gate));
 HR(s_d3d.cmd_queue->lpVtbl->Wait(s_d3d.cmd_queue,gate,1));submit();
 REQUIRE(!gpu_pso_retire());REQUIRE(s_d3d.vp_tex[a].res==ra&&s_d3d.vp_tex[b].res==rbefore&&s_d3d.vp_tex[a].used&&s_d3d.vp_tex[b].used);
 HR(gate->lpVtbl->Signal(gate,1));REQUIRE(gpu_pso_retire());pixels(0,0,255);
 address_retire();texture_retention_begin_batch();REQUIRE(s_d3d.vp_tex_bytes<=texture_retention_budget());REQUIRE(s_d3d.vp_tex_evictions>0);
 for(unsigned i=0;i<10;i++)texture_slot_release(&s_d3d.vp_tex[i]);REQUIRE(!s_d3d.vp_tex_bytes);
 }
 if(argc==1||strcmp(argv[1],"cube-only")) {
  reset_commands();int slot=upload(0);REQUIRE(slot>=0);
  capture(slot);submit();REQUIRE(vp_upload_retire(NULL));pixels_argb(vm_base+0x1000);
  address_retire();texture_retention_begin_batch();reset_commands();
  vm_base[0x1000+7]^=255; /* outside the actual sparse 64-byte sampling step */
  REQUIRE(upload(0)==slot);capture(slot);submit();REQUIRE(vp_upload_retire(NULL));pixels_argb(vm_base+0x1000);
  address_retire();texture_retention_begin_batch();
  for(unsigned i=0;i<10;i++)texture_slot_release(&s_d3d.vp_tex[i]);
 }
 const unsigned cube_off=0x100000,cube_stride=81920;
 for(unsigned f=0;f<6;f++)for(unsigned j=0;j<128*128;j++){
  uint8_t*v=vm_base+cube_off+f*cube_stride+j*4;v[0]=255;v[1]=(f&1)?255:0;v[2]=(f&2)?255:0;v[3]=(f&4)?255:0;
 }
 reset_commands();int cube=vp_upload_tex_slot_impl(cube_off,128,128,0xa5,1,2,NULL);REQUIRE(cube>=0);
 for(unsigned f=0;f<6;f++){
  capture_face(cube,f);submit();REQUIRE(vp_upload_retire(NULL));pixels_argb(vm_base+cube_off+f*cube_stride);
  address_retire();texture_retention_begin_batch();reset_commands();
 }
 vm_base[cube_off+5*cube_stride+7]^=255;
 REQUIRE(vp_upload_tex_slot_impl(cube_off,128,128,0xa5,1,2,NULL)==cube);
 capture_face(cube,5);submit();REQUIRE(vp_upload_retire(NULL));pixels_argb(vm_base+cube_off+5*cube_stride);
 address_retire();texture_retention_begin_batch();
 for(unsigned i=0;i<10;i++)texture_slot_release(&s_d3d.vp_tex[i]);REQUIRE(!s_d3d.vp_tex_bytes);
 finish: ;
 unsigned errors=0;uint64_t count=info->lpVtbl->GetNumStoredMessagesAllowedByRetrievalFilter(info);
 for(uint64_t i=0;i<count;i++){SIZE_T n=0;HR(info->lpVtbl->GetMessage(info,i,NULL,&n));D3D12_MESSAGE*m=malloc(n);REQUIRE(m);HR(info->lpVtbl->GetMessage(info,i,m,&n));if(m->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){fprintf(stderr,"D3D12 validation: %s\n",m->pDescription);++errors;}free(m);}
 printf("D3D12 validation messages=%llu errors=%u\n",(unsigned long long)count,errors);REQUIRE(!errors);printf("PASS GPU phase=%s allocations=%llu evictions=%llu peak=%llu\n",argc>1?argv[1]:"full",(unsigned long long)s_d3d.vp_tex_allocations,(unsigned long long)s_d3d.vp_tex_evictions,(unsigned long long)s_d3d.vp_tex_peak_bytes);
 if(gate)gate->lpVtbl->Release(gate);if(rb)rb->lpVtbl->Release(rb);info->lpVtbl->Release(info);s_d3d.cmd_list->lpVtbl->Release(s_d3d.cmd_list);s_d3d.cmd_allocators[0]->lpVtbl->Release(s_d3d.cmd_allocators[0]);s_d3d.fence->lpVtbl->Release(s_d3d.fence);CloseHandle(s_d3d.fence_event);s_d3d.cmd_queue->lpVtbl->Release(s_d3d.cmd_queue);s_d3d.srv_heap->lpVtbl->Release(s_d3d.srv_heap);s_d3d.device->lpVtbl->Release(s_d3d.device);free(vm_base);free(s_d3d.vp_tex);return 0;
}
