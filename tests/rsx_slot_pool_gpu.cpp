#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include "rsx_slot_pool.h"
#include <vector>
using Microsoft::WRL::ComPtr;
static unsigned checks,failures;
static void check(bool ok,const char* label){checks++;if(!ok){failures++;printf("FAIL %s\n",label);}}
static void require(HRESULT hr,const char* label){if(FAILED(hr)){fprintf(stderr,"%s %08X\n",label,(unsigned)hr);throw std::runtime_error(label);}}
static ComPtr<ID3DBlob> compile(const char* source,const char* target){ComPtr<ID3DBlob>b,e;HRESULT h=D3DCompile(source,strlen(source),"authored",nullptr,nullptr,"main",target,0,0,&b,&e);if(FAILED(h)&&e)fwrite(e->GetBufferPointer(),1,e->GetBufferSize(),stderr);require(h,"D3DCompile");return b;}

int main(int argc,char** argv){try{
 bool hardware=false,legacy=false;for(int i=1;i<argc;i++){if(!strcmp(argv[i],"--hardware"))hardware=true;else if(!strcmp(argv[i],"--fixed96"))legacy=true;else return 2;}
 ComPtr<ID3D11Device>d;ComPtr<ID3D11DeviceContext>ctx;require(D3D11CreateDevice(nullptr,hardware?D3D_DRIVER_TYPE_HARDWARE:D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&d,nullptr,&ctx),"device");
 auto vb=compile("float4 main(uint n:SV_VertexID):SV_POSITION{return float4(n==1?3:-1,n==2?3:-1,0,1);}","vs_5_0");
 auto pb=compile("Texture2D m:register(t0);SamplerState s:register(s0);float4 main():SV_TARGET{return float4(0,0,0,1-m.SampleLevel(s,float2(.5,.5),0).r);}","ps_5_0");
 ComPtr<ID3D11VertexShader>vs;ComPtr<ID3D11PixelShader>ps;require(d->CreateVertexShader(vb->GetBufferPointer(),vb->GetBufferSize(),nullptr,&vs),"VS");require(d->CreatePixelShader(pb->GetBufferPointer(),pb->GetBufferSize(),nullptr,&ps),"PS");ctx->VSSetShader(vs.Get(),nullptr,0);ctx->PSSetShader(ps.Get(),nullptr,0);
 std::vector<ComPtr<ID3D11Texture2D>> owners;std::vector<ComPtr<ID3D11ShaderResourceView>> views;ID3D11ShaderResourceView** slots=nullptr;unsigned cap=0;bool zero=true;ID3D11ShaderResourceView* selected=nullptr;
 for(unsigned i=0;i<100;i++){
  if(i==cap){unsigned next=cap;void* p=rsx_slot_pool_grow(slots,cap,i+1,96,legacy?96:320,sizeof(*slots),&next);if(!p)break;slots=(ID3D11ShaderResourceView**)p;for(unsigned j=cap;j<next;j++)if(slots[j])zero=false;cap=next;}
  D3D11_TEXTURE2D_DESC td={};td.Width=td.Height=td.ArraySize=td.MipLevels=1;td.SampleDesc.Count=1;td.Format=DXGI_FORMAT_R8_UNORM;td.BindFlags=D3D11_BIND_SHADER_RESOURCE;unsigned char white=255;D3D11_SUBRESOURCE_DATA init={&white,1,1};ComPtr<ID3D11Texture2D>t;ComPtr<ID3D11ShaderResourceView>v;require(d->CreateTexture2D(&td,&init,&t),"mask");require(d->CreateShaderResourceView(t.Get(),nullptr,&v),"mask SRV");slots[i]=v.Get();owners.push_back(t);views.push_back(v);if(i==99)selected=v.Get();
 }
 check(zero,"new slots initialized");check(slots[0]==views[0].Get(),"earlier binding survives growth");ctx->PSSetShaderResources(0,1,&selected);
 D3D11_SAMPLER_DESC sd={};sd.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sd.MaxLOD=D3D11_FLOAT32_MAX;ComPtr<ID3D11SamplerState>sampler;require(d->CreateSamplerState(&sd,&sampler),"sampler");auto sr=sampler.Get();ctx->PSSetSamplers(0,1,&sr);
 D3D11_TEXTURE2D_DESC td={};td.Width=td.Height=td.MipLevels=td.ArraySize=1;td.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;td.SampleDesc.Count=1;td.BindFlags=D3D11_BIND_RENDER_TARGET;ComPtr<ID3D11Texture2D>rt,rb;require(d->CreateTexture2D(&td,nullptr,&rt),"target");td.BindFlags=0;td.Usage=D3D11_USAGE_STAGING;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;require(d->CreateTexture2D(&td,nullptr,&rb),"readback");ComPtr<ID3D11RenderTargetView>view;require(d->CreateRenderTargetView(rt.Get(),nullptr,&view),"RTV");auto raw=view.Get();ctx->OMSetRenderTargets(1,&raw,nullptr);
 D3D11_BLEND_DESC bd={};auto& blend=bd.RenderTarget[0];blend.BlendEnable=TRUE;blend.SrcBlend=D3D11_BLEND_SRC_ALPHA;blend.DestBlend=D3D11_BLEND_INV_SRC_ALPHA;blend.SrcBlendAlpha=D3D11_BLEND_ONE;blend.DestBlendAlpha=D3D11_BLEND_ZERO;blend.BlendOp=blend.BlendOpAlpha=D3D11_BLEND_OP_ADD;blend.RenderTargetWriteMask=15;ComPtr<ID3D11BlendState>bs;require(d->CreateBlendState(&bd,&bs),"blend");ctx->OMSetBlendState(bs.Get(),nullptr,~0u);
 D3D11_RASTERIZER_DESC rd={};rd.FillMode=D3D11_FILL_SOLID;rd.CullMode=D3D11_CULL_NONE;rd.DepthClipEnable=TRUE;ComPtr<ID3D11RasterizerState>raster;require(d->CreateRasterizerState(&rd,&raster),"raster");ctx->RSSetState(raster.Get());D3D11_VIEWPORT vp={0,0,1,1,0,1};ctx->RSSetViewports(1,&vp);ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);float clear[]={.25f,.5f,.75f,1};ctx->ClearRenderTargetView(view.Get(),clear);ctx->Draw(3,0);ctx->CopyResource(rb.Get(),rt.Get());D3D11_MAPPED_SUBRESOURCE map={};require(ctx->Map(rb.Get(),0,D3D11_MAP_READ,0,&map),"map");const float* pixel=(const float*)map.pData;printf("mask100 output=%g,%g,%g,%g\n",pixel[0],pixel[1],pixel[2],pixel[3]);check(pixel[0]==.25f&&pixel[1]==.5f&&pixel[2]==.75f,"mask100 preserves terrain instead of opaque black");ctx->Unmap(rb.Get(),0);free(slots);printf("checks=%u failures=%u\n",checks,failures);return failures?1:0;
}catch(const std::exception& e){fprintf(stderr,"%s\n",e.what());return 2;}}
