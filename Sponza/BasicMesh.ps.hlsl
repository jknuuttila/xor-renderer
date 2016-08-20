#include "BasicMesh.sig.h"

struct PSInput
{
    float4 uv : TEXCOORD0;
};

[RootSignature(BASICMESH_ROOT_SIGNATURE)]
float4 main(PSInput i) : SV_Target
{
    float4 diffuse = diffuseTex.Sample(bilinear, i.uv.xy);
    return float4(diffuse.rgb, 1);
}
