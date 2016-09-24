#include "RenderTerrain.sig.h"

struct PSInput
{
    float4 worldPos : POSITION0;
    float4 svPos    : SV_Position;
};

[RootSignature(RENDERTERRAIN_ROOT_SIGNATURE)]
float4 main(PSInput i) : SV_Target
{
    float h = (i.worldPos.y - heightMin) / (heightMax - heightMin);
    float3 color = h ;//* sqrt(i.svPos.z);
	return float4(color, 1);
}
