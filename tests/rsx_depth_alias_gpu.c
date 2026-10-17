/* Game-free pixel checks for address-keyed D3D12 depth attachments. */
#define WIN32_LEAN_AND_MEAN
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dxgi1_4.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <d3dcompiler.h>
#include "rsx_d3d12_depth_alias.h"

static ID3D12Device* device;
static ID3D12CommandQueue* queue;
static ID3D12CommandAllocator* allocator;
static ID3D12GraphicsCommandList* list;
static ID3D12Fence* fence;
static ID3D12DescriptorHeap *rtv_heap, *dsv_heap;
static ID3D12Resource* targets[3];
static ID3D12Resource* readback;
static ID3D12RootSignature* root;
static ID3D12PipelineState *pipeline, *stencil_pipeline;
static ID3D12InfoQueue* messages;
static rsx_depth_alias_cache aliases;
static ID3D12Resource* zeta[2];
static UINT rtv_step, dsv_step;
static UINT64 fence_value;
static HANDLE event_handle;
static UINT slots[2];
static int legacy, last_target = -1;
static unsigned checks, failures;

static void check(HRESULT result, const char* operation)
{
    if (FAILED(result)) {
        fprintf(stderr, "%s HRESULT=0x%08lX\n", operation, (unsigned long)result);
        exit(2);
    }
}

static D3D12_CPU_DESCRIPTOR_HANDLE rtv(UINT index)
{
    D3D12_CPU_DESCRIPTOR_HANDLE handle;
    rtv_heap->lpVtbl->GetCPUDescriptorHandleForHeapStart(rtv_heap, &handle);
    handle.ptr += (SIZE_T)index * rtv_step;
    return handle;
}

static D3D12_CPU_DESCRIPTOR_HANDLE depth(UINT index)
{
    D3D12_CPU_DESCRIPTOR_HANDLE result;
    dsv_heap->lpVtbl->GetCPUDescriptorHandleForHeapStart(dsv_heap,&result);
    result.ptr+=(SIZE_T)index*dsv_step;return result;
}

static void submit(void)
{
    ID3D12CommandList* commands[] = {(ID3D12CommandList*)list};
    check(list->lpVtbl->Close(list), "close");
    queue->lpVtbl->ExecuteCommandLists(queue, 1, commands);
    check(queue->lpVtbl->Signal(queue, fence, ++fence_value), "signal");
    if (fence->lpVtbl->GetCompletedValue(fence) < fence_value) {
        check(fence->lpVtbl->SetEventOnCompletion(fence, fence_value, event_handle), "fence event");
        if (WaitForSingleObject(event_handle, 30000) != WAIT_OBJECT_0) exit(3);
    }
    check(device->lpVtbl->GetDeviceRemovedReason(device), "device status");
    check(allocator->lpVtbl->Reset(allocator), "allocator reset");
    check(list->lpVtbl->Reset(list, allocator, NULL), "list reset");
}

static void clear_depth(UINT index, D3D12_CLEAR_FLAGS flags, float value, UINT8 stencil)
{
    list->lpVtbl->ClearDepthStencilView(list, depth(index), flags, value, stencil, 0, NULL);
}

static void pixel(UINT target, UINT32 expected, const char* name)
{
    D3D12_RESOURCE_BARRIER barrier = {0};
    D3D12_TEXTURE_COPY_LOCATION source = {0}, destination = {0};
    D3D12_RANGE range = {0, 32 * 256};
    void* data = NULL;
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = targets[target];
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    list->lpVtbl->ResourceBarrier(list, 1, &barrier);
    source.pResource = targets[target];
    source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    destination.pResource = readback;
    destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    destination.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    D3D12_RESOURCE_DESC size;targets[target]->lpVtbl->GetDesc(targets[target],&size);
    destination.PlacedFootprint.Footprint.Width = (UINT)size.Width;
    destination.PlacedFootprint.Footprint.Height = size.Height;
    destination.PlacedFootprint.Footprint.Depth = 1;
    destination.PlacedFootprint.Footprint.RowPitch = 256;
    list->lpVtbl->CopyTextureRegion(list, &destination, 0, 0, 0, &source, NULL);
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    list->lpVtbl->ResourceBarrier(list, 1, &barrier);
    submit();
    check(readback->lpVtbl->Map(readback, 0, &range, &data), "readback map");
    D3D12_RESOURCE_DESC desc;targets[target]->lpVtbl->GetDesc(targets[target],&desc);
    UINT32 actual=expected;
    for(UINT y=0;y<desc.Height;y++)for(UINT x=0;x<desc.Width;x++){
        UINT32 value;memcpy(&value,(char*)data+y*256+x*4,4);
        if(value!=expected)actual=value;
    }
    readback->lpVtbl->Unmap(readback, 0, NULL);
    ++checks;
    failures += actual != expected;
    printf("%s %s pixel=%08X expected=%08X\n", actual == expected ? "PASS" : "FAIL",
        name, actual, expected);
}

int main(int argc, char** argv)
{
    int hardware = 0;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--hardware")) hardware = 1;
        else if (!strcmp(argv[i], "--legacy")) legacy = 1;
        else return 2;
    }
    ID3D12Debug* debug = NULL;
    if (SUCCEEDED(D3D12GetDebugInterface(&IID_ID3D12Debug, (void**)&debug))) {
        debug->lpVtbl->EnableDebugLayer(debug);
        debug->lpVtbl->Release(debug);
    } else printf("Debug layer unavailable\n");
    IDXGIFactory4* factory = NULL;
    IUnknown* adapter = NULL;
    check(CreateDXGIFactory1(&IID_IDXGIFactory4, (void**)&factory), "factory");
    if (!hardware) check(factory->lpVtbl->EnumWarpAdapter(factory, &IID_IUnknown,
        (void**)&adapter), "WARP adapter");
    check(D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device,
        (void**)&device), "device");
    if (adapter) adapter->lpVtbl->Release(adapter);
    factory->lpVtbl->Release(factory);
    device->lpVtbl->QueryInterface(device, &IID_ID3D12InfoQueue, (void**)&messages);
    D3D12_COMMAND_QUEUE_DESC queue_desc = {0};
    check(device->lpVtbl->CreateCommandQueue(device, &queue_desc, &IID_ID3D12CommandQueue,
        (void**)&queue), "queue");
    check(device->lpVtbl->CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_DIRECT,
        &IID_ID3D12CommandAllocator, (void**)&allocator), "allocator");
    check(device->lpVtbl->CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT,
        allocator, NULL, &IID_ID3D12GraphicsCommandList, (void**)&list), "list");
    check(device->lpVtbl->CreateFence(device, 0, D3D12_FENCE_FLAG_NONE,
        &IID_ID3D12Fence, (void**)&fence), "fence");
    event_handle = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (!event_handle) return 2;
    D3D12_DESCRIPTOR_HEAP_DESC heap = {0};
    heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    heap.NumDescriptors = 3;
    check(device->lpVtbl->CreateDescriptorHeap(device, &heap, &IID_ID3D12DescriptorHeap,
        (void**)&rtv_heap), "RTV heap");
    heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    heap.NumDescriptors = 2;
    check(device->lpVtbl->CreateDescriptorHeap(device, &heap, &IID_ID3D12DescriptorHeap,
        (void**)&dsv_heap), "DSV heap");
    rtv_step = device->lpVtbl->GetDescriptorHandleIncrementSize(device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    dsv_step = device->lpVtbl->GetDescriptorHandleIncrementSize(device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    D3D12_HEAP_PROPERTIES properties = {0};
    D3D12_RESOURCE_DESC resource = {0};
    properties.Type = D3D12_HEAP_TYPE_DEFAULT;
    resource.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resource.Width = resource.Height = 32;
    resource.DepthOrArraySize = resource.MipLevels = 1;
    resource.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    resource.SampleDesc.Count = 1;
    resource.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    resource.Format=DXGI_FORMAT_D24_UNORM_S8_UINT;
    resource.Flags=D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    for(UINT i=0;i<2;i++){
      check(device->lpVtbl->CreateCommittedResource(device,&properties,0,&resource,
        D3D12_RESOURCE_STATE_DEPTH_WRITE,NULL,&IID_ID3D12Resource,(void**)&zeta[i]),"depth resource");
      device->lpVtbl->CreateDepthStencilView(device,zeta[i],NULL,depth(i));
    }
    properties.Type = D3D12_HEAP_TYPE_READBACK;
    memset(&resource, 0, sizeof resource);
    resource.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resource.Width = 32 * 256;
    resource.Height = resource.DepthOrArraySize = resource.MipLevels = 1;
    resource.SampleDesc.Count = 1;
    resource.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    check(device->lpVtbl->CreateCommittedResource(device, &properties, D3D12_HEAP_FLAG_NONE,
        &resource, D3D12_RESOURCE_STATE_COPY_DEST, NULL, &IID_ID3D12Resource,
        (void**)&readback), "readback buffer");
    int indices[2];
    for(UINT i=0;i<2;i++){
      check(rsx_depth_alias_prepare(&aliases,device,zeta[i],32,32,&indices[i]),"alias prepare");
      targets[i]=aliases.views[indices[i]].color;
    }
    int again=-1;
    check(rsx_depth_alias_prepare(&aliases,device,zeta[0],32,32,&again),"reuse prepare");
    if(again!=indices[0]||aliases.count!=2)return 4;
    clear_depth(0,D3D12_CLEAR_FLAG_DEPTH|D3D12_CLEAR_FLAG_STENCIL,1,0x31);
    clear_depth(1,D3D12_CLEAR_FLAG_DEPTH|D3D12_CLEAR_FLAG_STENCIL,.5f,0x92);
    rsx_depth_alias_refresh(&aliases,indices[0],list,1);
    rsx_depth_alias_refresh(&aliases,indices[1],list,1);
    pixel(0,0xff31ffff,"far depth and stencil byte mapping");
    pixel(1,0x80920000,"half depth and independent stencil");
    if(rsx_depth_alias_refresh(&aliases,indices[0],list,1))return 5;
    clear_depth(0,D3D12_CLEAR_FLAG_DEPTH,0,0);
    rsx_depth_alias_refresh(&aliases,indices[0],list,legacy?1:2);
    pixel(0,0x00310000,"depth-only write refreshes alias preserving stencil");
    clear_depth(0,D3D12_CLEAR_FLAG_STENCIL,0,0xef);
    rsx_depth_alias_refresh(&aliases,indices[0],list,3);
    pixel(0,0x00ef0000,"stencil-only write refreshes alias preserving depth");
    clear_depth(0,D3D12_CLEAR_FLAG_DEPTH,.25f,0);
    rsx_depth_alias_refresh(&aliases,indices[0],list,4);
    pixel(0,0x40ef0000,"quarter depth byte mapping");
    pixel(1,0x80920000,"other depth source remains unchanged");
    int cropped;check(rsx_depth_alias_prepare(&aliases,device,zeta[0],17,13,&cropped),"cropped alias");
    targets[2]=aliases.views[cropped].color;
    clear_depth(0,D3D12_CLEAR_FLAG_DEPTH|D3D12_CLEAR_FLAG_STENCIL,0x123456/16777216.f,0x7b);
    rsx_depth_alias_refresh(&aliases,cropped,list,5);
    pixel(2,0x127b5634,"nontrivial depth bytes and cropped odd dimensions");
    submit();
    if(messages){
      for(UINT64 i=0;i<messages->lpVtbl->GetNumStoredMessages(messages);i++){
        SIZE_T size=0;messages->lpVtbl->GetMessage(messages,i,NULL,&size);
        D3D12_MESSAGE* m=malloc(size);check(messages->lpVtbl->GetMessage(messages,i,m,&size),"message");
        if(m->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){fprintf(stderr,"GPU validation: %s\n",m->pDescription);failures++;}
        free(m);
      }
    }
    rsx_depth_alias_release(&aliases);
    for(int i=0;i<2;i++)zeta[i]->lpVtbl->Release(zeta[i]);
    CloseHandle(event_handle);
    printf("%s %s checks=%u failures=%u\n",hardware?"hardware":"warp",legacy?"stale-control":"depth-alias",checks,failures);
    return failures?1:0;
}
