#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include "rsx_shader_cache.h"
using Microsoft::WRL::ComPtr;
static unsigned checks,failures;
static void check(bool ok,const char* label){checks++;if(!ok){failures++;printf("FAIL %s\n",label);}}
static void require(HRESULT hr,const char* label){if(FAILED(hr)){fprintf(stderr,"%s %08X\n",label,(unsigned)hr);throw std::runtime_error(label);}}
static void retain(void*,void* b){((ID3DBlob*)b)->AddRef();}
static void release(void*,void* b){((ID3DBlob*)b)->Release();}
static ComPtr<ID3DBlob> compile(const char* source,const char* target){ComPtr<ID3DBlob>b,e;HRESULT h=D3DCompile(source,strlen(source),"authored",nullptr,nullptr,"main",target,0,0,&b,&e);if(FAILED(h)&&e)fwrite(e->GetBufferPointer(),1,e->GetBufferSize(),stderr);require(h,"D3DCompile");return b;}
int main(int argc,char** argv){try{
 bool hardware=false,bypass=false;for(int i=1;i<argc;i++){if(!strcmp(argv[i],"--hardware"))hardware=true;else if(!strcmp(argv[i],"--no-cache"))bypass=true;else return 2;}
 ComPtr<ID3D11Device>d;ComPtr<ID3D11DeviceContext>ctx;
 require(D3D11CreateDevice(nullptr,hardware?D3D_DRIVER_TYPE_HARDWARE:D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&d,nullptr,&ctx),"device");
 const char* vs="float4 main(uint n:SV_VertexID):SV_POSITION{return float4(n==1?3:-1,n==2?3:-1,0,1);}";
 const char* ps="float4 main():SV_TARGET{return float4(.25,.5,.75,1);}";
 auto vb=compile(vs,"vs_5_0"),baseline=compile(ps,"ps_5_0");ComPtr<ID3D11VertexShader>v;
 require(d->CreateVertexShader(vb->GetBufferPointer(),vb->GetBufferSize(),nullptr,&v),"VS");ctx->VSSetShader(v.Get(),nullptr,0);
 D3D11_TEXTURE2D_DESC td={};td.Width=td.Height=td.MipLevels=td.ArraySize=1;td.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;td.SampleDesc.Count=1;td.BindFlags=D3D11_BIND_RENDER_TARGET;
 ComPtr<ID3D11Texture2D>rt,readback;require(d->CreateTexture2D(&td,nullptr,&rt),"target");td.BindFlags=0;td.Usage=D3D11_USAGE_STAGING;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;require(d->CreateTexture2D(&td,nullptr,&readback),"readback");
 ComPtr<ID3D11RenderTargetView>view;require(d->CreateRenderTargetView(rt.Get(),nullptr,&view),"RTV");auto raw=view.Get();ctx->OMSetRenderTargets(1,&raw,nullptr);D3D11_VIEWPORT vp={0,0,1,1,0,1};ctx->RSSetViewports(1,&vp);ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
 D3D11_RASTERIZER_DESC rd={};rd.FillMode=D3D11_FILL_SOLID;rd.CullMode=D3D11_CULL_NONE;rd.DepthClipEnable=TRUE;ComPtr<ID3D11RasterizerState>raster;require(d->CreateRasterizerState(&rd,&raster),"raster");ctx->RSSetState(raster.Get());
 rsx_shader_cache cache={};rsx_shader_blob_ops ops={nullptr,retain,release};rsx_shader_key key={ps,strlen(ps),"authored","main","ps_5_0",0,0};unsigned compilations=0;
 for(unsigned i=0;i<4;i++){
  ComPtr<ID3DBlob>code;if(!bypass)code.Attach((ID3DBlob*)rsx_shader_cache_get(&cache,&key,&ops));
  if(!code){code=compile(ps,"ps_5_0");compilations++;if(!bypass&&!rsx_shader_cache_put(&cache,&key,code.Get(),code->GetBufferSize(),&ops))throw std::runtime_error("cache admission");}
  check(code->GetBufferSize()==baseline->GetBufferSize()&&!memcmp(code->GetBufferPointer(),baseline->GetBufferPointer(),code->GetBufferSize()),"byte-exact uncached compiler output");
  ComPtr<ID3D11PixelShader>shader;require(d->CreatePixelShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&shader),"PS");ctx->PSSetShader(shader.Get(),nullptr,0);
  D3D11_BLEND_DESC bd={};auto& b=bd.RenderTarget[0];b.BlendEnable=i&1;b.SrcBlend=b.SrcBlendAlpha=D3D11_BLEND_ONE;b.DestBlend=b.DestBlendAlpha=D3D11_BLEND_ZERO;b.BlendOp=D3D11_BLEND_OP_ADD;b.BlendOpAlpha=(i&2)?D3D11_BLEND_OP_MAX:D3D11_BLEND_OP_ADD;b.RenderTargetWriteMask=15;
  ComPtr<ID3D11BlendState>blend;require(d->CreateBlendState(&bd,&blend),"blend");ctx->OMSetBlendState(blend.Get(),nullptr,~0u);float clear[4]={};ctx->ClearRenderTargetView(view.Get(),clear);ctx->Draw(3,0);ctx->CopyResource(readback.Get(),rt.Get());
  D3D11_MAPPED_SUBRESOURCE mapped={};require(ctx->Map(readback.Get(),0,D3D11_MAP_READ,0,&mapped),"map");const float* pixel=(const float*)mapped.pData;if(pixel[0]!=.25f||pixel[1]!=.5f||pixel[2]!=.75f||pixel[3]!=1.f)printf("pixel %u: %g %g %g %g\n",i,pixel[0],pixel[1],pixel[2],pixel[3]);check(pixel[0]==.25f&&pixel[1]==.5f&&pixel[2]==.75f&&pixel[3]==1.f,"pipeline variant pixels");ctx->Unmap(readback.Get(),0);
 }
 check(compilations==1,"one compile across four pipeline variants");rsx_shader_cache_clear(&cache,&ops);check(!cache.count&&!cache.bytes,"bounded cache cleanup");printf("checks=%u failures=%u\n",checks,failures);return failures?1:0;
}catch(const std::exception& e){fprintf(stderr,"%s\n",e.what());return 2;}}
