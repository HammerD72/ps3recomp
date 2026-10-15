/* Guest addressing only. Filtering/LOD retain the existing VP policy; full
 * guest filtering (including convolution) is a separate unfinished contract. */
#ifndef RSX_D3D12_ADDRESS_H
#define RSX_D3D12_ADDRESS_H
#include <d3d12.h>
#include <stdio.h>
#include <string.h>
static unsigned rsx_address_sampler_mask(const char* hlsl)
{
    unsigned mask = 0;
    for (unsigned u = 0; u < 16; ++u) {
        char token[32]; snprintf(token, sizeof(token), "rsx_samp[%u]", u);
        if (strstr(hlsl, token)) mask |= 1u << u;
    }
    return mask;
}
static int rsx_address_mode(unsigned value, D3D12_TEXTURE_ADDRESS_MODE* out)
{
    switch (value) {
    case 1: *out = D3D12_TEXTURE_ADDRESS_MODE_WRAP; return 1;
    case 2: *out = D3D12_TEXTURE_ADDRESS_MODE_MIRROR; return 1;
    case 3: *out = D3D12_TEXTURE_ADDRESS_MODE_CLAMP; return 1;
    case 4: *out = D3D12_TEXTURE_ADDRESS_MODE_BORDER; return 1;
    case 6: *out = D3D12_TEXTURE_ADDRESS_MODE_MIRROR_ONCE; return 1;
    default: return 0; /* Legacy clamp/mirror-border need shader support. */
    }
}
static int rsx_address_sampler(unsigned unit, unsigned address, unsigned border,
                               D3D12_SAMPLER_DESC* out)
{
    memset(out, 0, sizeof(*out));
    if (!rsx_address_mode(address & 15, &out->AddressU) ||
        !rsx_address_mode((address >> 8) & 15, &out->AddressV) ||
        !rsx_address_mode((address >> 16) & 15, &out->AddressW)) return 0;
    out->Filter = unit == 0 ? D3D12_FILTER_MIN_MAG_MIP_POINT :
                             D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    out->MaxLOD = D3D12_FLOAT32_MAX;
    out->MaxAnisotropy = 1;
    out->ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    if (out->AddressU == D3D12_TEXTURE_ADDRESS_MODE_BORDER ||
        out->AddressV == D3D12_TEXTURE_ADDRESS_MODE_BORDER ||
        out->AddressW == D3D12_TEXTURE_ADDRESS_MODE_BORDER) {
        out->BorderColor[0] = (float)((border >> 16) & 255) / 255.0f;
        out->BorderColor[1] = (float)((border >> 8) & 255) / 255.0f;
        out->BorderColor[2] = (float)(border & 255) / 255.0f;
        out->BorderColor[3] = (float)(border >> 24) / 255.0f;
    }
    return 1;
}
#endif
