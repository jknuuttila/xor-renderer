#include "BasicMesh.sig.h"

struct PSInput
{
    float4 uv : TEXCOORD0;
};

[RootSignature(BASICMESH_ROOT_SIGNATURE)]
float4 main(PSInput i) : SV_Target
{
    return float4(i.uv.x, 0, i.uv.y, 1);
}
