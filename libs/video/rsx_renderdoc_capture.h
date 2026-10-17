/* Optional RenderDoc app API. Capture one full presented frame, including
 * intermediate offscreen submissions. Default off; no desktop automation.
 * Public API header: RenderDoc v1.46, MIT, copyright retained in third_party. */
#ifndef RSX_RENDERDOC_CAPTURE_H
#define RSX_RENDERDOC_CAPTURE_H
#include "../../third_party/renderdoc/renderdoc_app.h"
static RENDERDOC_API_1_6_0* s_rdoc;
static int s_rdoc_active, s_rdoc_done;
static const char* s_rdoc_arm;
static void rdoc_fail(const char* why)
{
    fprintf(stderr,"[RSXRDOC] %s (Win32=%lu)\n",why,(unsigned long)GetLastError());
    abort();
}
static void rdoc_init(void)
{
    const char* dll=getenv("RSX_RENDERDOC_DLL");
    if(!dll) return;
    s_rdoc_arm=getenv("RSX_RENDERDOC_ARM_FILE");
    const char* path=getenv("RSX_RENDERDOC_CAPTURE_PATH");
    if(!*dll || !s_rdoc_arm || !*s_rdoc_arm || !path || !*path)
        rdoc_fail("requested capture lacks owned paths");
    HMODULE module=LoadLibraryExA(dll,NULL,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if(!module) rdoc_fail("DLL load failed");
    pRENDERDOC_GetAPI get=(pRENDERDOC_GetAPI)GetProcAddress(module,"RENDERDOC_GetAPI");
    if(!get || get(eRENDERDOC_API_Version_1_6_0,(void**)&s_rdoc)!=1 || !s_rdoc)
        rdoc_fail("API1.6 unavailable");
    s_rdoc->SetCaptureKeys(NULL,0);s_rdoc->SetFocusToggleKeys(NULL,0);
    s_rdoc->MaskOverlayBits(eRENDERDOC_Overlay_None,eRENDERDOC_Overlay_None);
    s_rdoc->SetCaptureFilePathTemplate(path);
    if(!s_rdoc->SetCaptureOptionU32(eRENDERDOC_Option_AllowVSync,1) ||
       !s_rdoc->SetCaptureOptionU32(eRENDERDOC_Option_AllowFullscreen,1))
        rdoc_fail("presentation options refused");
    fprintf(stderr,"[RSXRDOC] ready; capture begins after an armed real present\n");
    /* Keep the module loaded for process lifetime: hooks own wrapped objects. */
}
static void rdoc_name_pso(ID3D12PipelineState* pso,unsigned fp,int vs)
{
    static unsigned serial;
    if(!s_rdoc) return;
    wchar_t name[128];
    _snwprintf(name,127,L"RSX FP %08X VS %d pipeline %u",fp,vs,++serial);name[127]=0;
    if(FAILED(pso->lpVtbl->SetName(pso,name))) rdoc_fail("PSO naming failed");
}
/* Called only after successful swap-chain Present, never after a partial
 * record-buffer flush. Start at one boundary and end at the following one. */
static void rdoc_present(ID3D12Device* device,HWND window)
{
    if(!s_rdoc || s_rdoc_done) return;
    if(s_rdoc_active) {
        if(!s_rdoc->EndFrameCapture(device,window)) rdoc_fail("frame capture failed");
        uint32_t count=s_rdoc->GetNumCaptures(),length=0;uint64_t stamp=0;
        if(!count || !s_rdoc->GetCapture(count-1,NULL,&length,&stamp) || length>32768)
            rdoc_fail("capture path unavailable");
        char path[32768];length=sizeof(path);
        if(!s_rdoc->GetCapture(count-1,path,&length,&stamp)) rdoc_fail("capture path query failed");
        fprintf(stderr,"[RSXRDOC] completed path=%s\n",path);
        s_rdoc_active=0;s_rdoc_done=1;return;
    }
    DWORD attributes=GetFileAttributesA(s_rdoc_arm);
    if(attributes==INVALID_FILE_ATTRIBUTES) {
        if(GetLastError()!=ERROR_FILE_NOT_FOUND && GetLastError()!=ERROR_PATH_NOT_FOUND)
            rdoc_fail("arm marker query failed");
        return;
    }
    if(attributes&FILE_ATTRIBUTE_DIRECTORY) rdoc_fail("arm marker is a directory");
    s_rdoc->StartFrameCapture(device,window);
    if(!s_rdoc->IsFrameCapturing()) rdoc_fail("frame capture did not start");
    s_rdoc_active=1;fprintf(stderr,"[RSXRDOC] started after real present\n");
}
#endif
