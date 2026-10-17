/* Opt-in device-removal evidence. Does not change draws or wait bounds. */
#ifndef RSX_D3D12_FAULT_H
#define RSX_D3D12_FAULT_H
#include <windows.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int rsx_gpu_fault_enabled(void) {
    const char *value=getenv("D3D12_DRED");
    return value && !strcmp(value,"1");
}
static void rsx_gpu_fault_enable(void) {
    static const GUID iid={0x82bc481c,0x6b9b,0x4030,{0xae,0xdb,0x7e,0xe3,0xd1,0xdf,0x1e,0x63}};
    ID3D12DeviceRemovedExtendedDataSettings *settings=NULL;
    HRESULT hr;
    if(!rsx_gpu_fault_enabled())return;
    hr=D3D12GetDebugInterface(&iid,(void **)&settings);
    if(FAILED(hr) || !settings) {
        fprintf(stderr,"[D3D12-DRED] settings unavailable: %08lX\n",(unsigned long)hr);
        return;
    }
    settings->lpVtbl->SetAutoBreadcrumbsEnablement(settings,D3D12_DRED_ENABLEMENT_FORCED_ON);
    settings->lpVtbl->SetPageFaultEnablement(settings,D3D12_DRED_ENABLEMENT_FORCED_ON);
    settings->lpVtbl->Release(settings);
    fprintf(stderr,"[D3D12-DRED] breadcrumbs and page faults requested before device creation\n");
}
static void rsx_gpu_fault_name(ID3D12Object *object,const wchar_t *name) {
    HRESULT hr;
    if(!rsx_gpu_fault_enabled() || !object)return;
    hr=object->lpVtbl->SetName(object,name);
    if(FAILED(hr))fprintf(stderr,"[D3D12-DRED] object name failed: %08lX\n",(unsigned long)hr);
}
static void rsx_gpu_fault_allocations(const char *kind,const D3D12_DRED_ALLOCATION_NODE1 *node) {
    unsigned count=0;
    for(;node && count<64;node=node->pNext,++count)
        fprintf(stderr,"[D3D12-DRED] %s allocation=%u type=%u object=%p name=%.120s wide=%.120ls\n",
            kind,count,(unsigned)node->AllocationType,(void *)node->pObject,
            node->ObjectNameA?node->ObjectNameA:"",node->ObjectNameW?node->ObjectNameW:L"");
    if(node)fprintf(stderr,"[D3D12-DRED] %s allocations truncated at64\n",kind);
}
static void rsx_gpu_fault_report(ID3D12Device *device) {
    static const GUID dred_iid={0x9727a022,0xcf1d,0x4dda,{0x9e,0xba,0xef,0xfa,0x65,0x3f,0xc5,0x06}};
    static const GUID iq_iid={0x0742a90b,0xc387,0x483f,{0xb9,0x46,0x30,0xa7,0xe4,0xe6,0x14,0x58}};
    static volatile LONG reported;
    ID3D12DeviceRemovedExtendedData1 *dred=NULL;
    ID3D12InfoQueue *iq=NULL;
    D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT1 bread={0};
    D3D12_DRED_PAGE_FAULT_OUTPUT1 page={0};
    const D3D12_AUTO_BREADCRUMB_NODE1 *node;
    HRESULT reason,hr;
    unsigned nodes=0;
    if(!rsx_gpu_fault_enabled() || !device)return;
    reason=device->lpVtbl->GetDeviceRemovedReason(device);
    fprintf(stderr,"[D3D12-FAULT] tick=%llu device_reason=%08lX thread=%lu\n",
        (unsigned long long)GetTickCount64(),(unsigned long)reason,(unsigned long)GetCurrentThreadId());
    /* A CPU deadline can precede device removal. Do not consume the report then. */
    if(SUCCEEDED(reason) || InterlockedCompareExchange(&reported,1,0))return;
    hr=device->lpVtbl->QueryInterface(device,&iq_iid,(void **)&iq);
    if(SUCCEEDED(hr) && iq) {
        UINT64 count=iq->lpVtbl->GetNumStoredMessages(iq);
        fprintf(stderr,"[D3D12-FAULT] queued validation messages=%llu\n",(unsigned long long)count);
        for(UINT64 i=0;i<count;++i) {
            SIZE_T size=0;D3D12_MESSAGE *message;
            hr=iq->lpVtbl->GetMessage(iq,i,NULL,&size);
            if(FAILED(hr)) {fprintf(stderr,"[D3D12-FAULT] message size query failed=%08lX\n",(unsigned long)hr);continue;}
            message=(D3D12_MESSAGE *)malloc(size);
            if(!message) {fprintf(stderr,"[D3D12-FAULT] message allocation failed bytes=%llu\n",(unsigned long long)size);break;}
            hr=iq->lpVtbl->GetMessage(iq,i,message,&size);
            if(FAILED(hr))fprintf(stderr,"[D3D12-FAULT] message query failed=%08lX\n",(unsigned long)hr);
            else if(message->Severity<=D3D12_MESSAGE_SEVERITY_ERROR)
                fprintf(stderr,"[D3D12-FAULT-IQ][sev=%d id=%d] %s\n",(int)message->Severity,(int)message->ID,message->pDescription);
            free(message);
        }
        iq->lpVtbl->Release(iq);
    } else fprintf(stderr,"[D3D12-FAULT] validation interface unavailable=%08lX\n",(unsigned long)hr);
    hr=device->lpVtbl->QueryInterface(device,&dred_iid,(void **)&dred);
    if(FAILED(hr) || !dred) {fprintf(stderr,"[D3D12-DRED] report interface unavailable=%08lX\n",(unsigned long)hr);return;}
    hr=dred->lpVtbl->GetAutoBreadcrumbsOutput1(dred,&bread);
    fprintf(stderr,"[D3D12-DRED] breadcrumbs result=%08lX head=%p\n",(unsigned long)hr,(void *)bread.pHeadAutoBreadcrumbNode);
    node=SUCCEEDED(hr)?bread.pHeadAutoBreadcrumbNode:NULL;
    for(;node && nodes<64;node=node->pNext,++nodes) {
        UINT value=node->pLastBreadcrumbValue?*node->pLastBreadcrumbValue:0;
        fprintf(stderr,"[D3D12-DRED] node=%u list=%p queue=%p count=%u value=%u name=%.120s wide=%.120ls\n",
            nodes,(void *)node->pCommandList,(void *)node->pCommandQueue,node->BreadcrumbCount,value,
            node->pCommandListDebugNameA?node->pCommandListDebugNameA:"",node->pCommandListDebugNameW?node->pCommandListDebugNameW:L"");
        if(node->pCommandHistory && node->BreadcrumbCount<=65536 && value<=node->BreadcrumbCount) {
            UINT begin=value>12?value-12:0,end=node->BreadcrumbCount;
            if(end-value>13)end=value+13;
            for(UINT i=begin;i<end;++i)
                fprintf(stderr,"[D3D12-DRED] node=%u history[%u]=%u\n",nodes,i,(unsigned)node->pCommandHistory[i]);
        } else fprintf(stderr,"[D3D12-DRED] history unavailable or beyond supported64K range\n");
    }
    if(node)fprintf(stderr,"[D3D12-DRED] breadcrumb nodes truncated at64\n");
    hr=dred->lpVtbl->GetPageFaultAllocationOutput1(dred,&page);
    fprintf(stderr,"[D3D12-DRED] page result=%08lX VA=%016llX\n",(unsigned long)hr,(unsigned long long)page.PageFaultVA);
    if(SUCCEEDED(hr)) {
        rsx_gpu_fault_allocations("existing",page.pHeadExistingAllocationNode);
        rsx_gpu_fault_allocations("freed",page.pHeadRecentFreedAllocationNode);
    }
    dred->lpVtbl->Release(dred);fflush(stderr);
}
#endif
