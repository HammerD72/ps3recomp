/* Game-free B8 packing from an actual D3D12 RGBA8 render target. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <cstdio>
#include <stdexcept>
#include <vector>
#include "rsx_surface_readback.h"
using Microsoft::WRL::ComPtr;

static void require(HRESULT hr,const char *operation) {
    if(FAILED(hr)) {
        std::fprintf(stderr,"%s: HRESULT=0x%08lX\n",operation,(unsigned long)hr);
        throw std::runtime_error(operation);
    }
}
struct Event {
    HANDLE handle=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    ~Event(){if(handle)CloseHandle(handle);}
};
int main() try {
    ComPtr<ID3D12Debug> debug;
    require(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)),"debug layer");
    debug->EnableDebugLayer();
    ComPtr<IDXGIFactory4> factory;
    require(CreateDXGIFactory1(IID_PPV_ARGS(&factory)),"DXGI factory");
    ComPtr<IDXGIAdapter> warp;
    require(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)),"WARP adapter");
    ComPtr<ID3D12Device> device;
    require(D3D12CreateDevice(warp.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)),"device");
    ComPtr<ID3D12InfoQueue> messages;
    require(device.As(&messages),"validation messages");
    D3D12_COMMAND_QUEUE_DESC qd={};
    ComPtr<ID3D12CommandQueue> queue;
    require(device->CreateCommandQueue(&qd,IID_PPV_ARGS(&queue)),"queue");
    ComPtr<ID3D12CommandAllocator> allocator;
    require(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)),"allocator");
    ComPtr<ID3D12GraphicsCommandList> list;
    require(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)),"list");

    constexpr unsigned width=257,height=2,guest_pitch=264;
    D3D12_RESOURCE_DESC td={};td.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    td.Width=width;td.Height=height;td.DepthOrArraySize=1;td.MipLevels=1;
    td.Format=DXGI_FORMAT_R8G8B8A8_UNORM;td.SampleDesc.Count=1;
    td.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_HEAP_PROPERTIES hp={};hp.Type=D3D12_HEAP_TYPE_DEFAULT;
    ComPtr<ID3D12Resource> texture;
    require(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&td,D3D12_RESOURCE_STATE_RENDER_TARGET,nullptr,IID_PPV_ARGS(&texture)),"texture");
    D3D12_DESCRIPTOR_HEAP_DESC hd={};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;hd.NumDescriptors=1;
    ComPtr<ID3D12DescriptorHeap> heap;
    require(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap)),"RTV heap");
    auto rtv=heap->GetCPUDescriptorHandleForHeapStart();
    device->CreateRenderTargetView(texture.Get(),nullptr,rtv);
    for(unsigned y=0;y<height;y++)for(unsigned x=0;x<width;x++) {
        const float color[4]={13.f/255.f,71.f/255.f,float((x+17*y)%256)/255.f,193.f/255.f};
        D3D12_RECT rect={LONG(x),LONG(y),LONG(x+1),LONG(y+1)};
        list->ClearRenderTargetView(rtv,color,1,&rect);
    }
    D3D12_RESOURCE_BARRIER barrier={};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource=texture.Get();barrier.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore=D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_SOURCE;
    list->ResourceBarrier(1,&barrier);
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint={};UINT64 bytes=0;
    device->GetCopyableFootprints(&td,0,1,0,&footprint,nullptr,nullptr,&bytes);
    unsigned host_pitch=rsx_surface_readback_host_pitch(width,9,D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);
    if(host_pitch!=footprint.Footprint.RowPitch || rsx_surface_readback_host_bpp(9)!=4)
        throw std::runtime_error("RGBA8 host storage contract");
    hp.Type=D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC bd={};bd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;bd.Width=bytes;
    bd.Height=1;bd.DepthOrArraySize=1;bd.MipLevels=1;bd.SampleDesc.Count=1;bd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> buffer;
    require(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&bd,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&buffer)),"readback buffer");
    D3D12_TEXTURE_COPY_LOCATION src={};src.pResource=texture.Get();src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_TEXTURE_COPY_LOCATION dst={};dst.pResource=buffer.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=footprint;
    list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
    require(list->Close(),"close list");
    ID3D12CommandList *submitted[]={list.Get()};queue->ExecuteCommandLists(1,submitted);
    ComPtr<ID3D12Fence> fence;
    require(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)),"fence");
    Event event;if(!event.handle)throw std::runtime_error("event");
    require(queue->Signal(fence.Get(),1),"signal");
    require(fence->SetEventOnCompletion(1,event.handle),"fence event");
    if(WaitForSingleObject(event.handle,30000)!=WAIT_OBJECT_0)throw std::runtime_error("GPU fence deadline");
    require(device->GetDeviceRemovedReason(),"device status");
    void *mapped=nullptr;D3D12_RANGE range={0,SIZE_T(bytes)};
    require(buffer->Map(0,&range,&mapped),"map");
    std::vector<unsigned char> guest(guest_pitch*height,0xa5);
    int packed=rsx_surface_readback_rows(guest.data(),guest.size(),guest_pitch,mapped,size_t(bytes),footprint.Footprint.RowPitch,width,height,0x149);
    D3D12_RANGE empty={0,0};buffer->Unmap(0,&empty);
    if(packed)throw std::runtime_error("B8 conversion");
    for(unsigned y=0;y<height;y++)for(unsigned x=0;x<guest_pitch;x++)
        if(guest[y*guest_pitch+x]!=(x<width?(x+17*y)%256:0xa5))throw std::runtime_error("B8 blue byte or guest padding");
    unsigned errors=0;
    for(UINT64 i=0;i<messages->GetNumStoredMessagesAllowedByRetrievalFilter();i++) {
        SIZE_T size=0;require(messages->GetMessage(i,nullptr,&size),"message size");
        std::vector<unsigned char> storage(size);auto m=reinterpret_cast<D3D12_MESSAGE *>(storage.data());
        require(messages->GetMessage(i,m,&size),"message");
        std::fprintf(stderr,"D3D12 severity=%d: %s\n",int(m->Severity),m->pDescription);
        if(m->Severity<=D3D12_MESSAGE_SEVERITY_ERROR)errors++;
    }
    if(errors)throw std::runtime_error("D3D12 validation errors");
    std::printf("PASS: %u B8 pixels, host_pitch=%u guest_pitch=%u validation_errors=0\n",width*height,footprint.Footprint.RowPitch,guest_pitch);
    return 0;
} catch(const std::exception& e) {
    std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;
}
